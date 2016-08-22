/*  =========================================================================
    bios_outage_server - Bios outage server

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

#ifndef BIOS_OUTAGE_SERVER_H_INCLUDED
#define BIOS_OUTAGE_SERVER_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

//  @interface
//  BIOS outage server actor
//
//  Create new outage_server instance
//
//      zactor_t *outage = zactor_new (outage, NULL);
//
//  Destroy the instance
//
//      zactor_destroy (&outage);
//
//  Enable verbose logging
//
//      zstr_sendx (outage, "VERBOSE", NULL);
//
//  Connect to malamute endpoint with name
//
//      zstr_sendx (outage, "CONNECT", "outage1", NULL);
//
//  Let actor consume on stream STREAM pattern PATTERN
//
//      zstr_sendx (outage, "CONSUMER", "METRICS", ".*", NULL);
//
//  Let it produce messages on stream
//
//      zstr_sendx (outage, "PRODUCER", "ALERTS", NULL);
//
//  Change default timeout for internal poller. Value is in ms, default is 30 000
//
//      zstr_sendx (outage, "TIMEOUT", "1000", NULL);
//
// Change default time for asset expiry. Value us in seconds, default is 2 hours
//
//      zstr_sendx (outage, "ASSET-EXPIRY-SEC", "3", NULL);
//
AGENT_OUTAGE_EXPORT void
    bios_outage_server (zsock_t *pipe, void *args);

//  Self test of this class
AGENT_OUTAGE_EXPORT void
    bios_outage_server_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
