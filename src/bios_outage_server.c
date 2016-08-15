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
//  Static helper functions


// 1 - $TERM
// 0 - message processed and deleted


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




//  --------------------------------------------------------------------------
//  Create a new bios_outage_server
void
bios_outage_server (zsock_t *pipe, void *args)
{
    mlm_client_t *client = mlm_client_new ();
    assert (client);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    assert(poller);

    zsock_signal (pipe, 0);

    // poller timeout
    int64_t timeout = 2000;
    int64_t timestamp = zclock_mono ();

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

        int64_t now = zclock_mono();

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

        zstr_free(&string);
        zmsg_destroy(&message);
    }
    zpoller_destroy(&poller);
    mlm_client_destroy (&client);
}


//  --------------------------------------------------------------------------
//  Self test of this class

void
bios_outage_server_test (bool verbose)
{
    printf (" * bios_outage_server: \n");

    //  @selftest

    static const char *endpoint =  "ipc://malamute-test2";

    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND",endpoint, NULL);
    zclock_sleep (1000);

    zactor_t *outsvr = zactor_new (bios_outage_server, (void*) NULL);
    assert (outsvr);

    // actor commands
    zstr_sendx (outsvr, "ENDPOINT", endpoint, NULL);
    zstr_sendx (outsvr, "ENDPOINT",  NULL);
    zstr_sendx (outsvr, "KARCI", endpoint, "outsvr", NULL);
    zstr_sendx (outsvr, "ENDPOINT", endpoint, "outsvr", NULL);
    zclock_sleep (1000);

    zstr_sendx (outsvr, "CONSUMER", "ALERTS",".*", NULL);
    zstr_sendx (outsvr, "CONSUMER", "ALERTS", NULL);

    zstr_sendx (outsvr, "PRODUCER", "ALERTS", NULL);
    zstr_sendx (outsvr, "PRODUCER", NULL);


    // hello-world test
    mlm_client_t *sender = mlm_client_new();
    int rv = mlm_client_connect (sender, endpoint, 5000, "sender");
    assert (rv >= 0);

    // reply on certain message hello --> world
    zmsg_t *msg = zmsg_new ();
    zmsg_addstr (msg, "hello");

    printf("\ncekani na zpravu1\n");
    rv = mlm_client_sendto (sender,"outsvr", "subject", NULL, 1000, &msg);
    assert (rv >= 0);
    zclock_sleep (3000);
    
    zmsg_t *recv = mlm_client_recv (sender);
    assert (recv);

    char *recvmsg = zmsg_popstr(recv); 
    assert (streq(recvmsg,"world"));
    
    {
    zmsg_t *msg = zmsg_new ();
    zmsg_addstr (msg, "hello");

    rv = mlm_client_sendto (sender,"outsvr", "subject", NULL, 1000, &msg);
    assert (rv >= 0);
    zclock_sleep (1000);

    printf("\ncekame na zpravu2\n");
    zmsg_t *recv = mlm_client_recv (sender);
    assert (recv);

    char *recvmsg = zmsg_popstr(recv);
    assert (streq(recvmsg,"world"));

    zstr_free (&recvmsg);
    zmsg_destroy(&recv);
    zmsg_destroy(&msg);
    }
    
    zstr_free (&recvmsg);
    zmsg_destroy(&recv);
    mlm_client_destroy (&sender);
    zactor_destroy(&outsvr);
    zactor_destroy (&server);
    
    //  @end
    printf ("OK\n");
}
