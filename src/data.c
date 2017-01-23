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

#include "fty_outage_classes.h"

//  Structure of our class

struct _data_t {
    zhashx_t *assets;   // asset_name | (timestamp | ttl )
};

// ------------------------------------------------------------------------
// put data 
void
data_put (data_t *self, fty_proto_t  **proto_p) 
{
    assert (self);
    assert (proto_p);
    
    fty_proto_t *proto = *proto_p;
    if (!proto)
        return;

    // data from fty_proto
    const char *source = fty_proto_element_src (proto);
    uint64_t ttl = (uint64_t)fty_proto_ttl (proto);

    // getting timestamp from metrics
    uint64_t timestamp = fty_proto_aux_number (proto, "AGENT_CM_TIME", 0);
    uint64_t expiration_time = timestamp + 2*ttl;

    void *rv = zhashx_lookup (self->assets, source);
    if (!rv) {
        printf ("%s not in table\n", source);
        uint64_t *expiration_p = (uint64_t*) malloc (sizeof (uint64_t));
        *expiration_p = expiration_time;
        zhashx_insert (self->assets, source, (void*) expiration_p);
    }    
    else
    {
        printf ("adding %s to the table\n", source);
        uint64_t * expiration_p = (uint64_t*) rv;
        *expiration_p = expiration_time;
        printf(">>>>table size %zu\n", zhashx_size(self->assets));
    }
    
    fty_proto_destroy(proto_p);
}


// --------------------------------------------------------------------------
// get non-responding devices
zlistx_t
*data_get_dead (data_t *self)
{
    // list of devices
    zlistx_t *dead = zlistx_new();
     
    for (void *expiration = zhashx_first (self->assets); 
         expiration != NULL;                 
	     expiration = zhashx_next (self->assets))
    {
        //uint64_t now = zclock_time();
        uint64_t now = 6;
        printf (">>>cas %"PRIu64, now);
        printf(" \n");

        if ((uint64_t) expiration >= now)
        {
            void *source = (void*) zhashx_cursor(self->assets);
            zlistx_add_start (dead, source);
        }
    }    

    zlistx_destroy(&dead);         
    return dead;

}

    
//  --------------------------------------------------------------------------
//  Create a new data

data_t *
data_new (void)
{
    data_t *self = (data_t *) zmalloc (sizeof (data_t));
    assert (self);
    self -> assets = zhashx_new();
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




//  --------------------------------------------------------------------------
//  Self test of this class

void
data_test (bool verbose)
{
    printf (" * data: \n");
  
    //  aux data for matric
    zhash_t *aux = zhash_new();
    // zhash_autofree (aux);
    zhash_update(aux,"key1", "val1");
    zhash_update(aux,"AGENT_CM_TYPE" , "2");
    zhash_update(aux,"key2" , "val2");
    
    // create new metrics 
    zmsg_t *met_n = fty_proto_encode_metric (aux, "device", "UPS4", "100", "C", 5);
    fty_proto_t *proto_n = fty_proto_decode (&met_n);

    // update metric
    zmsg_t *met_u = fty_proto_encode_metric (aux, "device", "UPS3", "100", "C", 6);
    fty_proto_t *proto_u = fty_proto_decode (&met_u);
    
    // key .. source, val ...expiration
    data_t *data = data_new ();
    assert(data);
        
    data_put(data, &proto_u);
    data_put(data, &proto_n);

    char *UPS4exp = (char*)zhashx_lookup(data->assets,"UPS4");
    printf("expiration time of UPS4 %s", UPS4exp);
    //    void *UPS3exp = zhashx_lookup(data->assets,"UPS3");
    //printf("expiration time of UPS3 %"PRIu64, *(uint64_t*) UPS3exp);

    // give me dead devices
    data_get_dead(data);
    
    fty_proto_destroy(&proto_n);
    fty_proto_destroy(&proto_u);
    zmsg_destroy(&met_n);
    zmsg_destroy(&met_u); 
    zhash_destroy(&aux);
    data_destroy (&data);


    //  @end
    printf ("OK\n");
}
