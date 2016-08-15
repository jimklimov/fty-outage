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

struct characteristics_t {
    uint64_t timestamp;
    uint64_t ttl;
};



    
//  --------------------------------------------------------------------------
//  Create a new data

data_t *
data_new (void)
{
    data_t *self = (data_t *) zmalloc (sizeof (data_t));
    assert (self);
    //  Initialize class properties here
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
        //  Free class properties here
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
data_test (bool verbose)
{
    printf (" * data: ");

    //  @selftest
    //  Simple create/destroy test
    data_t *self = data_new ();
    assert (self);
    data_destroy (&self);
    //  @end
    printf ("OK\n");
}
