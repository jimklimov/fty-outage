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

//  Self test of this class
FTY_OUTAGE_EXPORT void
    data_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
