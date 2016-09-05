/*  =========================================================================
    data - Data

    Copyright (C) 2014 - 2015 Eaton                                        
                                                                           
    This program is free software; you can redistribute it and/or modify   
    it under the terms of the GNU General Public License as published by   
    the Free Software Foundation; either version 2 of the License, or      
    (at your option) any later version.                                    
                                                                           
    This program is distributed in the hope that it will be useful,        
    but WITHOUT ANY WARRANTY; without even the implied warranty of         
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          
    GNU General Public License for more details.                           
                                                                           
    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.            
    =========================================================================
*/

/*
@header
    data - Data
@discuss
@end
*/

#include "agent_outage_classes.h"

// it is used as TTL, but in formula we are waiting for ttl*2 ->
// so if we here would have 15 minutes-> the first alert will come in 30 minutes
#define DEFAULT_ASSET_EXPIRATION_TIME_SEC 15*60/2

//  Structure of our class
typedef struct _expiration_t {
    uint64_t ttl_sec; // [s] minimal ttl seen for some asset
    uint64_t last_time_seen_sec; // [s] time when  some metrics were seen for this asset
} expiration_t;

static expiration_t*
expiration_new (uint64_t default_expiry_sec)
{
    expiration_t *self = (expiration_t *) zmalloc (sizeof (expiration_t));
    if (self) {
        self->ttl_sec = default_expiry_sec;
    }
    return self;
}

static void
expiration_destroy (expiration_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        expiration_t *self = *self_p;
        free (self);
        *self_p = NULL;
    }
}

// set up new expected expiration time, given last seen time
// this function can only prolong exiration_time
static void
expiration_update (expiration_t *self, uint64_t new_time_seen_sec, bool verbose)
{
    // this will ensure, that we will not have 'experiation' time moving backwards!
    // Situation: at 03:33 metric with 24h average comes with 'time' = 00:00
    // ttl is 5 minutes -> new expiration date would be 00:05 BUT now already 3:33 !!
    // So we will create false alert!
    // This 'if' is a guard for this situation!
    if ( new_time_seen_sec > self->last_time_seen_sec )
        self->last_time_seen_sec = new_time_seen_sec;
    if ( verbose )
        zsys_debug ("last_seen_time[s]: %" PRIu64, self->last_time_seen_sec);
}

static void
expiration_update_ttl (expiration_t *self, uint64_t proposed_ttl, bool verbose)
{
    // ATTENTION: if minimum ttl for some asset is greater than DEFAULT_ASSET_EXPIRATION_TIME_SEC
    // it will be sending alerts every DEFAULT_ASSET_EXPIRATION_TIME_SEC

    // logic: we are looking for the minimum ttl
    if ( self->ttl_sec > proposed_ttl ) {
        self->ttl_sec = proposed_ttl;
    }
    if ( verbose )
        zsys_debug ("ttl[s]: %" PRIu64, self->ttl_sec);
}

static uint64_t
experiation_get (expiration_t *self)
{
    return self->last_time_seen_sec + self->ttl_sec * 2;
}

struct _data_t {
    bool verbose;
    zhashx_t *assets;           // asset_name => expiration time [s]
    uint64_t default_expiry_sec; // [s] default time for the asset, in what asset would be considered as not responding
};

//  --------------------------------------------------------------------------
//  Destroy the data
void
data_destroy (data_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        data_t *self = *self_p;
        zhashx_destroy(&self -> assets);
        free (self);
        *self_p = NULL;
    }
}

//  -----------------------------------------------------------------------
//  Create a new data
data_t *
data_new (void)
{
    data_t *self = (data_t *) zmalloc (sizeof (data_t));
    if (self) {
        self -> assets = zhashx_new();
        if ( self->assets ) {
            self->verbose = false;
            self->default_expiry_sec = DEFAULT_ASSET_EXPIRATION_TIME_SEC; 
            zhashx_set_destructor (self -> assets,  (zhashx_destructor_fn *) expiration_destroy);
        }
        else 
            data_destroy (&self);
    }
    return self;
}

//  -----------------------------------------------------------------------
//  Setup as verbose
void
data_set_verbose (data_t* self, bool verbose)
{
    assert (self);
    self->verbose = verbose;
}

//  ------------------------------------------------------------------------
//  Return default number of seconds in that newly added asset would expire
uint64_t
data_default_expiry (data_t* self)
{
    assert (self);
    return self->default_expiry_sec;
}

//  ------------------------------------------------------------------------
//  Set default number of seconds in that newly added asset would expire
void
data_set_default_expiry (data_t* self, uint64_t expiry_sec)
{
    assert (self);
    self->default_expiry_sec = expiry_sec;
}

//  ------------------------------------------------------------------------
//  put data 
void
data_put (data_t *self, bios_proto_t *proto) 
{
    assert (self);
    assert (proto);

    if (bios_proto_id (proto) == BIOS_PROTO_METRIC) {

        const char *asset_name = bios_proto_element_src (proto);

        expiration_t *e = (expiration_t *) zhashx_lookup (self->assets, asset_name);
        if ( e != NULL ) {
            // we know information about this asset
            
            // try to update ttl
            uint64_t ttl = bios_proto_ttl (proto);
            expiration_update_ttl (e, ttl, self->verbose);
            // need to compute new expiration time
            uint64_t now_sec = zclock_time () / 1000 ;
            uint64_t timestamp = bios_proto_aux_number (proto, "time", now_sec);
            zsys_debug ("timestamp=%" PRIu64, timestamp);
            expiration_update (e, timestamp, self->verbose);
        }
        // if asset is not known -> we are not interested!
    }
    else if (bios_proto_id (proto) == BIOS_PROTO_ASSET) {

        const char *operation = bios_proto_operation (proto);
        const char *asset_name = bios_proto_name (proto);
        if (self->verbose)
            zsys_debug ("asset: name=%s, operation=%s", asset_name, operation);

        // remove asset from cache
        if (    streq (operation, BIOS_PROTO_ASSET_OP_DELETE)
             || streq (bios_proto_aux_string (proto, BIOS_PROTO_ASSET_STATUS, ""), "retired") )
        {
            data_delete (self, asset_name);
        }
        else
        // other asset operations - add ups, epdu or sensors to the cache if not present
        if ( streq (bios_proto_aux_string (proto, BIOS_PROTO_ASSET_TYPE, ""), "device" ) ) {
            const char* sub_type = bios_proto_aux_string (proto, BIOS_PROTO_ASSET_SUBTYPE, "");
            if (   streq (sub_type, "ups")
                || streq (sub_type, "epdu")
                || streq (sub_type, "sensor"))
            {
                // this asset is not known yet -> add it to the cache
                expiration_t *e = (expiration_t *) zhashx_lookup (self->assets, asset_name );
                if ( e == NULL ) {
                    e = expiration_new (self->default_expiry_sec);
                    uint64_t now_sec = zclock_time() / 1000;
                    expiration_update (e, now_sec, self->verbose);
                    if ( self->verbose )
                        zsys_debug ("asset: ADDED: name = '%s', now = %" PRIu64 "s, expires_at=%" PRIu64 "s", asset_name, now_sec, experiation_get (e));
                    zhashx_insert (self->assets, asset_name, e);
                }
                else {
                    // intentionally left empty
                    // So, if we already knew this asset -> nothing to do
                }
            }
        }
    }
}
// --------------------------------------------------------------------------
// delete from cache
void
data_delete (data_t *self, const char* source)
{
    assert (self);
    assert (source);

    zhashx_delete (self->assets, source);
}

// --------------------------------------------------------------------------
// get non-responding devices 
zlistx_t *
data_get_dead (data_t *self)
{
    assert (self);
    // list of devices
    zlistx_t *dead = zlistx_new();

    uint64_t now_sec = zclock_time() / 1000;        
    if ( self->verbose ) {
        zsys_debug ("now=%" PRIu64 "s", now_sec);
    }
    for (expiration_t *e =  (expiration_t *) zhashx_first (self->assets); 
        e != NULL;                 
	    e = (expiration_t *) zhashx_next (self->assets))
    {
        void *asset_name = (void*) zhashx_cursor(self->assets);
        if ( self->verbose ) {
            zsys_debug ("asset: name=%s, ttl=%" PRIu64 ", expires_at=%" PRIu64, asset_name, e->ttl_sec, experiation_get (e));
        }
        if ( experiation_get (e) <= now_sec)
        {   
            assert(zlistx_add_start (dead, asset_name));
        }
    }    
    
    return dead;
}

// support fn for test
// - reads expiration time for device (source) from zhashx
uint64_t
zhashx_get_expiration_test (data_t *self, char *source)
{
    assert(self);
    expiration_t *e = (expiration_t *) zhashx_lookup (self->assets, source);
    return experiation_get (e);
}   

// print content of zlistx
void
zlistx_print_dead (zlistx_t *self) {
    zsys_debug ("zlistx_print_dead:");
    for (void *it = zlistx_first(self);
         it != NULL;
         it = zlistx_next(self))
    {
        zsys_debug ("\t%s",(char *) it);
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
data_test (bool verbose)
{
    printf (" * data: \n");

    expiration_t *e = expiration_new(10);
    expiration_destroy (&e);
    //  aux data for metric - var_name | msg issued
    zhash_t *aux = zhash_new();

    zhash_update(aux,"key1", "val1");
    zhash_update(aux,"time" , "2");
    zhash_update(aux,"key2" , "val2");
    
    // key | expiration (t+2*ttl)
    data_t *data = data_new ();
    data_set_verbose (data, verbose);
    assert(data);

    // get/set test
    assert (data_default_expiry (data) == DEFAULT_ASSET_EXPIRATION_TIME_SEC);
    data_set_default_expiry (data, 42);
    assert (data_default_expiry (data) == 42);
    data_set_default_expiry (data, 2);
    
    // create asset first
    zhash_t *asset_aux = zhash_new ();
    zhash_insert (asset_aux, "type", "device");
    zhash_insert (asset_aux, "subtype", "ups");
    zmsg_t *asset = bios_proto_encode_asset (asset_aux, "UPS4", "create", NULL);
    bios_proto_t *proto_n = bios_proto_decode (&asset);
    data_put(data, proto_n);
    bios_proto_destroy (&proto_n);
    zhash_destroy (&asset_aux);

    asset_aux = zhash_new ();
    zhash_insert (asset_aux, "type", "device");
    zhash_insert (asset_aux, "subtype", "ups");
    asset = bios_proto_encode_asset (asset_aux, "UPS3", "create", NULL);
    proto_n = bios_proto_decode (&asset);
    data_put(data, proto_n);
    bios_proto_destroy (&proto_n);
    zhash_destroy (&asset_aux);

    // create new metric UPS4 - exp NOK
    zmsg_t *met_n = bios_proto_encode_metric (aux, "device", "UPS4", "100", "C", 3);
    proto_n = bios_proto_decode (&met_n);
    data_put(data, proto_n);
    bios_proto_destroy (&proto_n);
    
    // create new metric UPS3 - exp NOT OK
    met_n = bios_proto_encode_metric (aux, "device", "UPS3", "100", "C", 1);
    proto_n = bios_proto_decode (&met_n);
    data_put(data, proto_n);
    bios_proto_destroy (&proto_n);
    zclock_sleep (5000);
    // give me dead devices
    zlistx_t *list = data_get_dead(data);
    if (verbose)
        zlistx_print_dead(list);
    assert (zlistx_size (list) == 2);
    
    zlistx_destroy (&list);

    // update metric - exp OK
    zhash_update(aux,"time" , "90000000000000");
    zmsg_t *met_u = bios_proto_encode_metric (aux, "device", "UPS4", "100", "C", 2);
    bios_proto_t *proto_u = bios_proto_decode (&met_u);
    data_put(data, proto_u);
    bios_proto_destroy (&proto_n);

    // give me dead devices
    list = data_get_dead(data);
    if (verbose)
        zlistx_print_dead(list);
    assert (zlistx_size (list) == 1);

    // test asset message
    zhash_destroy (&aux);
    aux = zhash_new ();
    zhash_insert (aux, "status", "active");
    zhash_insert (aux, "type", "device");
    zhash_insert (aux, BIOS_PROTO_ASSET_SUBTYPE, "epdu");
    zmsg_t *msg = bios_proto_encode_asset (aux, "PDU1", BIOS_PROTO_ASSET_OP_CREATE, NULL);
    bios_proto_t* bmsg = bios_proto_decode (&msg);
    data_put (data, bmsg);
    bios_proto_destroy (&bmsg);

    assert (zhashx_lookup (data->assets, "PDU1"));
    uint64_t now_sec = zclock_time() / 1000;
    uint64_t diff = zhashx_get_expiration_test (data, "PDU1") - now_sec;
    if (verbose)
        zsys_debug ("diff=%"PRIi64, diff);
    assert ( diff <= (data_default_expiry (data) * 2));
    // TODO: test it more
     
    zlistx_destroy(&list);
    bios_proto_destroy(&proto_n);
    bios_proto_destroy(&proto_u);
    zmsg_destroy(&met_n);
    zmsg_destroy(&met_u); 
    zhash_destroy(&aux);
    data_destroy (&data);

    //  @end
    printf ("OK\n");
}
