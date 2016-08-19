/*  =========================================================================
    bios_agent_outage - Agent outage

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
    bios_agent_outage - Agent outage
@discuss
@end
*/

#include "agent_outage_classes.h"

int main (int argc, char *argv [])
{
    bool verbose = false;
    int argn;
    for (argn = 1; argn < argc; argn++) {
        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("bios-agent-outage [options] ...");
            puts ("  --verbose / -v         verbose test output");
            puts ("  --help / -h            this information");
            return 0;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v"))
            verbose = true;
        else {
            printf ("Unknown option: %s\n", argv [argn]);
            return 1;
        }
    }
    //  Insert main code here
    if (verbose)
        zsys_info ("bios_agent_outage - Agent outage");

    zactor_t *self = zactor_new (bios_outage_server, NULL);
    if (verbose)
        zstr_sendx (self, "VERBOSE", NULL);
    zstr_sendx (self, "ENDPOINT", "ipc://@/malamute", "agent-outage", NULL);
    zstr_sendx (self, "CONSUMER", "METRICS", ".*", NULL);
    zstr_sendx (self, "CONSUMER", "_METRICS_SENSOR", ".*", NULL);
    zstr_sendx (self, "CONSUMER", "ASSETS", ".*", NULL);
    zstr_sendx (self, "PRODUCER", "ALERTS", NULL);
    zstr_sendx (self, "TIMEOUT", "30000", NULL);

    // src/malamute.c, under MPL license
    while (true) {
        char *message = zstr_recv (self);
        if (message) {
            puts (message);
            zstr_free (&message);
        }
        else {
            puts ("interrupted");
            break;
        }
    }

    zactor_destroy (&self);
    return 0;
}
