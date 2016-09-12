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

#ifndef DATA_H_INCLUDED
#define DATA_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _data_t data_t;

//  @interface
//  Create a new data
AGENT_OUTAGE_EXPORT data_t *
    data_new (void);

//  Destroy the data
AGENT_OUTAGE_EXPORT void
    data_destroy (data_t **self_p);

//  Return default number of seconds in that newly added asset would expire
AGENT_OUTAGE_EXPORT uint64_t
    data_default_expiry (data_t* self);

//  Set default number of seconds in that newly added asset would expire
AGENT_OUTAGE_EXPORT void
    data_set_default_expiry (data_t* self, uint64_t expiry_sec);

//  calculates metric expiration time for each asset
//  takes owneship of the message
AGENT_OUTAGE_EXPORT void
    data_put (data_t *self, bios_proto_t  **proto);

//  delete from cache
AGENT_OUTAGE_EXPORT void
    data_delete (data_t *self, const char* source);

//  Returns list of nonresponding devices, zlistx entries are refereces
AGENT_OUTAGE_EXPORT zlistx_t *
    data_get_dead (data_t *self);

//  get all sensors assigned to port 'port' on the device 'parent_name'
//  return NULL in case of memory issues
//  empty list if nothing was found
//  ownership of the list is transferd to the caller and he is responsible for destroying it
AGENT_OUTAGE_EXPORT zlist_t *
    data_get_sensors (data_t *self, const char *port, const char *parent_name);

//  update information about expiration time
//  return -1, if data are from future and are ignored as damaging
//  return 0 otherwise
AGENT_OUTAGE_EXPORT int
    data_touch_asset (data_t *self, const char *asset_name, uint64_t timestamp, uint64_t ttl, uint64_t now_sec);

//  Setup as verbose
AGENT_OUTAGE_EXPORT void
    data_set_verbose (data_t* self, bool verbose);

//  Self test of this class
AGENT_OUTAGE_EXPORT void
    data_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
