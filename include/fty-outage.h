/*  =========================================================================
    fty-outage - Agent that sends alerts when device does not communicate.

    Copyright (C) 2014 - 2019 Eaton

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

#ifndef FTY_OUTAGE_H_H_INCLUDED
#define FTY_OUTAGE_H_H_INCLUDED

//  Include the project library file
#include "fty_outage_library.h"

//  Add your own public definitions here, if you need them
// Default TTL of assets in maintenance mode
#define DEFAULT_MAINTENANCE_EXPIRATION  "3600"

#define DISABLE_MAINTENANCE 0
#define ENABLE_MAINTENANCE  1

#endif
