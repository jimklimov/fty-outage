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

/*
@header
    bios_outage_server - Bios outage server
@discuss
@end
*/

#include "agent_outage_classes.h"

//  --------------------------------------------------------------------------
//  Create a new bios_outage_server
void
bios_outage_server (zsock_t *pipe, void *args)
{
    mlm_client_t *client = mlm_client_new ();
    assert (client);

    int rv = mlm_client_connect (client, "ipc://malamute-test", 1000, "outage");
    assert (rv >= 0);
    zsock_signal (pipe, 0);

    while (!zsys_interrupted)
    {
        zmsg_t *message = mlm_client_recv (client);
        if (!message)
            break;

        char *string = zmsg_popstr(message);
        if (!string)
            continue;
        
        if (streq (string, "hello")) {
            zmsg_t *reply = zmsg_new ();
            assert (reply);
            
            int rv = zmsg_addstr(reply, "world");
            assert(rv == 0);

            const char *msgsender = mlm_client_sender (client);
            rv = mlm_client_sendto (client, msgsender, "Subject", NULL, 1000, &reply); 
            assert (rv == 0);
            
            zmsg_destroy(&reply);
            
        }
        else if (streq (string, "ukonci-sa")) {
            zstr_free (&string);
            zmsg_destroy (&message);
            break;
            
        }
        zstr_free(&string);        
        zmsg_destroy(&message);
    }
    
    mlm_client_destroy (&client);
}



//  --------------------------------------------------------------------------
//  Self test of this class

void
bios_outage_server_test (bool verbose)
{
    printf (" * bios_outage_server: ");

    //  @selftest

    
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND","ipc://malamute-test", NULL);
    zclock_sleep (100);
    
    
    zactor_t *outsvr = zactor_new (bios_outage_server, (void*) NULL);
    assert (outsvr);
    zclock_sleep (100);

    mlm_client_t *sender = mlm_client_new();
    int rv = mlm_client_connect (sender,"ipc://malamute-test", 100, "sender");
    assert (rv >= 0);

    zmsg_t *msg = zmsg_new ();
    zmsg_addstr (msg, "hello");

    rv = mlm_client_sendto (sender,"outage", "subject", NULL, 1000, &msg);
    assert (rv >= 0);
    zclock_sleep (100);

    zmsg_t *recv = mlm_client_recv (sender);
    assert (recv);

    char *recvmsg = zmsg_popstr(recv); 
    assert (streq(recvmsg,"world"));

    zmsg_t *sprava = zmsg_new ();
    assert (sprava);
    zmsg_addstr (sprava, "ukonci-sa");
    rv = mlm_client_sendto (sender, "outage", "subject", NULL, 1000, &sprava);
    assert (rv >= 0);
    
    zstr_free (&recvmsg);
    zmsg_destroy(&recv);
    mlm_client_destroy (&sender);
    printf ("post mlm_client_t\n");
    zactor_destroy(&outsvr);
    printf ("post zactor_destroy\n");
    zactor_destroy (&server);

    printf ("koncime\n");
    
    //  @end
    printf ("OK\n");
}
