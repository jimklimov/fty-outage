/*  =========================================================================
    fty_outage_server - 42ity outage server

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

/*
@header
    fty_outage_server - 42ity outage server
@discuss
@end
*/

#include "fty_outage_classes.h"

//  --------------------------------------------------------------------------
//  Static helper functions

/*
 * return values :
 * 1 - $TERM recieved
 * 0 - message processed and deleted
 */

static int
s_actor_commands (mlm_client_t *client, zmsg_t **message_p)
{
    assert(client);
    assert(message_p && *message_p);

    zmsg_t *message =  *message_p;

    char *command = zmsg_popstr(message);
    if (!command) {
        zmsg_destroy (message_p);
        zsys_warning ("Empty command.");
        return 0;
    }
    if (streq(command, "$TERM")) {
         zsys_info ("Got $TERM");
         zmsg_destroy (message_p);
         zstr_free (&command);
         return 1;
         
    }
    else
    if (streq(command, "ENDPOINT"))
    {
	    char *endpoint = zmsg_popstr (message);
		char *name = zmsg_popstr (message);
                
		if (endpoint && name) {
		    int rv = mlm_client_connect (client, endpoint, 1000, name);
            if (rv == -1) 
			    zsys_error("mlm_client_connect failed\n");
	    }
               
		zstr_free (&endpoint);
		zstr_free (&name);
            
    }    
    else
    if (streq (command, "CONSUMER"))
    {
        char *stream = zmsg_popstr(message);
        char *regex = zmsg_popstr(message);

        if (stream && regex) {
            int rv = mlm_client_set_consumer (client, stream, regex);                    
            if (rv == -1 )
                zsys_error("mlm_set_consumer failed");
        }
        
        zstr_free (&stream);
        zstr_free (&regex);                                
                
    }
    else
    if (streq (command, "PRODUCER"))
    {
        char *stream = zmsg_popstr(message);
                
        if (stream){
            int rv = mlm_client_set_producer (client, stream);
            if (rv == -1 )
                zsys_error ("mlm_client_set_producer");
        }
        zstr_free(&stream);
    }
	else {
        zsys_error ("Unknown actor command: %s.\n", command);
	}
            
    zstr_free (&command);
    zmsg_destroy (message_p);
    return 0;
}



// --------------------------------------------------------------------------
// Create a new fty_outage_server
void
fty_outage_server (zsock_t *pipe, void *args)
{
    mlm_client_t *client = mlm_client_new ();
    assert (client);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    assert(poller);

    zsock_signal (pipe, 0);

    //    poller timeout
    uint64_t timeout = 2000;
    uint64_t timestamp = zclock_mono ();

    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, timeout);

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                zsys_debug ("Poller terminated.");
                break;
            }
            else {
                zsys_debug ("Poller expired");
                printf("I am alive\n");
                timestamp = zclock_mono();            
                continue;
                }

            timestamp = zclock_mono();
        }

        uint64_t now = zclock_mono();

        if (now - timestamp  >= timeout){
            printf(" >>> I am alive. <<<\n");
            timestamp = zclock_mono ();
        }

        if (which == pipe) {
            zmsg_t *msg = zmsg_recv(pipe);
            assert (msg);

            int rv = s_actor_commands (client, &msg);
            if (rv == 1)
                break;
            continue;    
        }
        
        assert (which == mlm_client_msgpipe (client));

        zmsg_t *message = mlm_client_recv (client);
        if (!message)
            break;

        if (!is_fty_proto(message)) {
            zmsg_destroy(&message);
            continue;
        }
        
        fty_proto_t *proto = fty_proto_decode (&message);        
        assert (proto);
        fty_proto_print (proto);

        
        fty_proto_destroy (&proto);
        
    }
    zpoller_destroy(&poller);
    mlm_client_destroy (&client);
}

// --------------------------------------------------------------------------
// Self test of this class

void
fty_outage_server_test (bool verbose)
{
    printf (" * fty_outage_server: \n");

    //     @selftest

    static const char *endpoint =  "ipc://malamute-test2";

    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND",endpoint, NULL);
    zclock_sleep (1000);

    zactor_t *outsvr = zactor_new (fty_outage_server, (void*) NULL);
    assert (outsvr);

    //    actor commands
    zstr_sendx (outsvr, "ENDPOINT", endpoint, NULL);
    zstr_sendx (outsvr, "ENDPOINT",  NULL);
    zstr_sendx (outsvr, "KARCI", endpoint, "outsvr", NULL);
    zstr_sendx (outsvr, "ENDPOINT", endpoint, "outsvr", NULL);
    zclock_sleep (1000);

    zstr_sendx (outsvr, "CONSUMER", "xyz",".*", NULL);
    zstr_sendx (outsvr, "CONSUMER", "ALERTS", NULL);

    zstr_sendx (outsvr, "PRODUCER", "ALERTS", NULL);
    zstr_sendx (outsvr, "PRODUCER", NULL);


    //    hello-world test
    mlm_client_t *sender = mlm_client_new();
    int rv = mlm_client_connect (sender, endpoint, 5000, "sender");
    assert (rv >= 0);

    rv = mlm_client_set_producer (sender, "xyz"); // "xyz = stream"
    assert (rv >= 0);

    // create asset
    /*    zmsg_t *sendmsg = fty_proto_encode_asset (
        NULL,
        "UPS33",
        "update",
        NULL);
*/
    zmsg_t *sendmsg = fty_proto_encode_metric (
        NULL,
        "dev",
        "UPS33",
        "1",
        "c",
        10);

    rv = mlm_client_send (sender, "subject",  &sendmsg);
    assert (rv >= 0);
    zclock_sleep (1000);


    mlm_client_destroy (&sender);
    zactor_destroy(&outsvr);
    zactor_destroy (&server);
    
    //  @end
    printf ("OK\n");
}
