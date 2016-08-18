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

//  Structure of our class

struct _data_t {
    zhashx_t *assets;   // asset_name | (timestamp | ttl )
};

// ------------------------------------------------------------------------
// put data 
void
data_put (data_t *self, bios_proto_t  **proto_p) 
{
    assert (self);
    assert (proto_p);
    
    bios_proto_t *proto = *proto_p;
    if (!proto)
        return;

    // data from bios_proto message
    uint64_t timestamp = bios_proto_aux_number (proto, "time", zclock_time());
    const char *source = bios_proto_element_src (proto);
    uint64_t ttl = (uint64_t) bios_proto_ttl (proto);

    // getting timestamp from metrics
    uint64_t expiration_time = timestamp + 2*ttl;
    
    void *rv = zhashx_lookup (self->assets, source);
    if (!rv) {
        uint64_t *expiration_p = (uint64_t*) malloc (sizeof (uint64_t));
        *expiration_p = expiration_time;
        zhashx_insert (self->assets, source, (void*) expiration_p);       
    }    
    else
    {
        uint64_t *expiration_p = (uint64_t*) rv;
        *expiration_p = expiration_time;
    }
    
    bios_proto_destroy(proto_p);
}

// --------------------------------------------------------------------------
// get non-responding devices 
zlistx_t
*data_get_dead (data_t *self)
{
    // list of devices
    zlistx_t *dead = zlistx_new();

    for (void *expiration =  zhashx_first (self->assets); 
        expiration != NULL;                 
	    expiration = zhashx_next (self->assets))
    {
        uint64_t now = zclock_time();        
 
        if (*(uint64_t*) expiration <= now)
        {   
            void *source = (void*) zhashx_cursor(self->assets);
            assert(zlistx_add_start (dead, source));
           
        }
    }    
    
    return dead;
}



// ------------------------------------------------------------------------
// destructor for zhashx items
static void
s_free (void **x_p) {
    if (*x_p) {
        free ((uint32_t*) *x_p);
        *x_p = NULL;
    }
}
    
//  -----------------------------------------------------------------------
//  Create a new data
data_t *
data_new (void)
{
    data_t *self = (data_t *) zmalloc (sizeof (data_t));
    assert (self);
    self -> assets = zhashx_new();
    zhashx_set_destructor (self -> assets, s_free);
    assert (self -> assets);
    
    return self;
}


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

// support fn for test
// - reads expiration time for device (source) from zhashx
uint64_t
zhashx_get_expiration_test (data_t *self, char *source)
{
    assert(self);
    uint64_t *expiration = (uint64_t*) zhashx_lookup (self->assets, source);
    return *expiration;
}   

void
zlistx_print_dead (zlistx_t *self) {
    zsys_debug ("list of nonresponding devices: \n");
    for (void *it = zlistx_first(self);
         it != NULL;
         it = zlistx_next(self))
    {
        zsys_debug ("%s\n",(char *) it);
    }

}

//  --------------------------------------------------------------------------
//  Self test of this class

void
data_test (bool verbose)
{
    printf (" * data: \n");

    //  aux data for matric - var_name | msg issued
    zhash_t *aux = zhash_new();

    zhash_update(aux,"key1", "val1");
    zhash_update(aux,"time" , "2");
    zhash_update(aux,"key2" , "val2");
    
    // key | expiration (t+2*ttl)
    data_t *data = data_new ();
    assert(data);
    
    // create new metric UPS4 - exp NOK
    zmsg_t *met_n = bios_proto_encode_metric (aux, "device", "UPS4", "100", "C", 5);
    bios_proto_t *proto_n = bios_proto_decode (&met_n);
    data_put(data, &proto_n);
    
    // create new metric UPS3 - exp NOT OK
    met_n = bios_proto_encode_metric (aux, "device", "UPS3", "100", "C", 1);
    proto_n = bios_proto_decode (&met_n);
    data_put(data, &proto_n);
               
    // give me dead devices
    zlistx_t *list = data_get_dead(data);    
    zlistx_print_dead(list);
    
    zlistx_destroy (&list);

    // update metric - exp OK
    zhash_update(aux,"time" , "90000000000000");
    zmsg_t *met_u = bios_proto_encode_metric (aux, "device", "UPS4", "100", "C", 2);
    bios_proto_t *proto_u = bios_proto_decode (&met_u);
    data_put(data, &proto_u);

    // give me dead devices
    list = data_get_dead(data);
    zlistx_print_dead(list);
     
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
