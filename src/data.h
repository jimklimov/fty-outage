/*  =========================================================================
    data - Data

    Copyright (C) 2014 - 2017 Eaton                                        
                                                                           
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

#include "../include/fty_outage.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DATA_T_DEFINED
typedef struct _data_t data_t;
#define DATA_T_DEFINED
#endif

//  @interface
//  Create a new data
FTY_OUTAGE_EXPORT data_t *
    data_new (void);

//  Destroy the data
FTY_OUTAGE_EXPORT void
    data_destroy (data_t **self_p);

//  Return default number of seconds in that newly added asset would expire
FTY_OUTAGE_EXPORT uint64_t
    data_default_expiry (data_t* self);

//  Set default number of seconds in that newly added asset would expire
FTY_OUTAGE_EXPORT void
    data_set_default_expiry (data_t* self, uint64_t expiry_sec);

//  calculates metric expiration time for each asset
//  takes owneship of the message
FTY_OUTAGE_EXPORT void
    data_put (data_t *self, fty_proto_t  **proto);

//  delete from cache
FTY_OUTAGE_EXPORT void
    data_delete (data_t *self, const char* source);

//  Returns list of nonresponding devices, zlistx entries are refereces
FTY_OUTAGE_EXPORT zlistx_t *
    data_get_dead (data_t *self);

//  update information about expiration time
//  return -1, if data are from future and are ignored as damaging
//  return 0 otherwise
FTY_OUTAGE_EXPORT int
    data_touch_asset (data_t *self, const char *asset_name, uint64_t timestamp, uint64_t ttl, uint64_t now_sec);

//  Setup as verbose
FTY_OUTAGE_EXPORT void
    data_set_verbose (data_t* self, bool verbose);

//  Self test of this class
FTY_OUTAGE_EXPORT void
    data_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
