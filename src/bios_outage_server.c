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

    //    int rv = mlm_client_connect (client, "ipc://malamute-test", 1000, "outage");
    // assert (rv >= 0);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    assert(poller);
    
    zsock_signal (pipe, 0);

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
        }
        
        int64_t now = zclock_mono();
        
        if (now - timestamp  >= timeout){
            printf(" >>> I am alive. <<<\n"); 
            timestamp = zclock_mono ();
        }
        
        if (which == pipe) {
            zmsg_t *msg = zmsg_recv(pipe);
            assert (msg);
            
            char *command = zmsg_popstr(msg);
            if (!command) {
                zmsg_destroy (&msg);
                zsys_debug ("Empty command.");
                continue;
            }
            
            if (streq(command, "$TERM")) {
                zmsg_destroy (&msg);
                zstr_free (&command);
                break;
            }
            else if (streq(command, "ENDPOINT")) {
		char *endpoint = zmsg_popstr (msg);
		char *name = zmsg_popstr (msg);
		if (endpoint && name) {
			int rv = mlm_client_connect (client, endpoint, 1000, name);
			if (rv >= 0) {
				printf ("mlm_client_connest OK\n");
			}
		}
		else {
			printf ("BAD ENDPOINT MESSAGE\n");

		}
		zstr_free (&endpoint);
		zstr_free (&name);
            }    
            else if (streq (command, "CONSUMER")) {
                char *stream = zmsg_popstr(msg);
                assert(stream);
                char *regex = zmsg_popstr(msg);
                assert(regex);
                
                zstr_free (&stream);
                zstr_free (&regex);                                
                zstr_free (&command);
                zmsg_destroy (&msg);
                continue;
            }
            else if (streq (command, "PRODUCER")){
                char *stream = zmsg_popstr(msg);
                assert(stream);
                
                zstr_free (&command);
                zmsg_print (msg);
                zmsg_destroy (&msg);
                continue;
                 
            }
	    else {
                zsys_debug ("Unknown command: %s. Terminating.\n", command);
	    }
                zstr_free (&command);
                zmsg_destroy (&msg);
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
    printf (" * bios_outage_server: ");

    //  @selftest
    static const char *endpoint =  "ipc://malamute-test2";
    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND",endpoint, NULL);
    zclock_sleep (100);

    
    zactor_t *outsvr = zactor_new (bios_outage_server, (void*) NULL);
    assert (outsvr);

    zstr_sendx (outsvr, "ENDPOINT", endpoint, NULL);
    zstr_sendx (outsvr, "ENDPOINT",  NULL);
    zstr_sendx (outsvr, "KARCI", endpoint, "outsvr", NULL);
    zstr_sendx (outsvr, "ENDPOINT", endpoint, "outsvr", NULL);
    zclock_sleep (100);

    mlm_client_t *sender = mlm_client_new();
    int rv = mlm_client_connect (sender, endpoint, 100, "sender");
    assert (rv >= 0);

    zmsg_t *msg = zmsg_new ();
    zmsg_addstr (msg, "hello");

    printf("\ncekani na zpravu1\n");
    rv = mlm_client_sendto (sender,"outsvr", "subject", NULL, 1000, &msg);
    assert (rv >= 0);
    zclock_sleep (1000);
    
    zmsg_t *recv = mlm_client_recv (sender);
    assert (recv);

    char *recvmsg = zmsg_popstr(recv); 
    assert (streq(recvmsg,"world"));
    
    /*
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
    */
           
    {
    zmsg_t *msg = zmsg_new ();
    zmsg_addstr (msg, "hello");

    rv = mlm_client_sendto (sender,"outsvr", "subject", NULL, 1000, &msg);
    assert (rv >= 0);
    zclock_sleep (100);

    printf("\ncekame na zpravu2\n");
    zmsg_t *recv = mlm_client_recv (sender);
    assert (recv);

    char *recvmsg = zmsg_popstr(recv); 
    assert (streq(recvmsg,"world"));
    
    zstr_free (&recvmsg);
    zmsg_destroy(&recv);
    }
    
    zstr_free (&recvmsg);
    zmsg_destroy(&recv);
    mlm_client_destroy (&sender);
    zactor_destroy(&outsvr);
    zactor_destroy (&server);

    //  @end
    printf ("OK\n");
}
