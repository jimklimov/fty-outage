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
#define TIMEOUT 30000   //wait at least 30 seconds

#include "agent_outage_classes.h"
#include "data.h"
//  --------------------------------------------------------------------------
//  Static helper functions

static void
s_send_outage_alert_for (mlm_client_t *client, const char* source, const char* state) {
    zmsg_t *msg = bios_proto_encode_alert (
            NULL,
            "outage",
            source,
            state,
            "WARNING",
            "Device does not provide expected data, probably offline",
            zclock_time (),
            "EMAIL|SMS");
    char *subject = zsys_sprintf ("%s/%s@%s",
        "outage",
        "WARNING",
        source);
    zmsg_print (msg);
    mlm_client_send (client, subject, &msg);
    zstr_free (&subject);
}

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
            zsys_debug ("ENDPOINT: %s/%s", endpoint, name);
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
            zsys_debug ("CONSUMER: %s/%s", stream, regex);
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
// Create a new bios_outage_server
void
bios_outage_server (zsock_t *pipe, void *args)
{
    static void *TRUE = (void*) "true";   // hack to allow us to pretend zhash is set
                                          // basically we don't care about value, just it must be != NULL

    mlm_client_t *client = mlm_client_new ();
    assert (client);
    data_t *assets = data_new ();
    assert (assets);
    zhash_t *active_alerts = zhash_new ();
    assert (active_alerts);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);
    zsys_debug ("mlm_client_msgpipe=<%p>", (void*) mlm_client_msgpipe (client));
    assert(poller);

    zsock_signal (pipe, 0);

    //    poller timeout
    uint64_t now = zclock_mono ();
    uint64_t last_dead_check = now;

    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, TIMEOUT);

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                zsys_info ("Terminating.");
                break;
            }
        }

        // send alerts
        now = zclock_mono ();
        if (zpoller_expired (poller) || (now - last_dead_check) > TIMEOUT) {
            zsys_debug ("poller expired");
            zlistx_t *dead_devices = data_get_dead (assets);
            zsys_debug ("dead_devices.size=%zu", zlistx_size (dead_devices));
            for (void *it = zlistx_first (dead_devices);
                       it != NULL;
                       it = zlistx_next (dead_devices))
            {
                const char* source = (const char*) zlistx_cursor (dead_devices);
                zsys_debug ("\tsource=%s", source);
                if (!zhash_lookup (active_alerts, source)) {
                    zsys_debug ("\t\tsend alert for source=%s", source);
                    s_send_outage_alert_for (client, source, "ACTIVE");
                    zhash_insert (active_alerts, source, TRUE);
                }
            }
            zlistx_destroy (&dead_devices);
        }

        zsys_debug ("which=<%p>", (void*) which);
        zsys_debug ("\tmlm_client_msgpipe=<%p>", (void*) mlm_client_msgpipe (client));
        if (which == pipe) {
            zsys_debug ("which == pipe");
            zmsg_t *msg = zmsg_recv(pipe);
            if (!msg)
                break;

            int rv = s_actor_commands (client, &msg);
            if (rv == 1)
                break;
            continue;
        }
        // react on incoming messages
        else
        if (which == mlm_client_msgpipe (client)) {

            zmsg_t *message = mlm_client_recv (client);
            if (!message)
                break;

            if (!is_bios_proto(message)) {
                zmsg_destroy(&message);
                continue;
            }

            bios_proto_t *bmsg = bios_proto_decode (&message);
            if (bmsg) {
                // resolve sent alert
                if (bios_proto_id (bmsg) == BIOS_PROTO_METRIC) {
                    const char* source = bios_proto_element_src (bmsg);
                    if (zhash_lookup (active_alerts, source)) {
                        s_send_outage_alert_for (client, source, "RESOLVED");
                        zhash_delete (active_alerts, source);
                    }
                }
                // add to cache
                zsys_debug ("data_put");
                data_put (assets, &bmsg);
            }
            bios_proto_destroy (&bmsg);
            continue;
        }
<<<<<<< 0100668a368d38cacd09d23a1459eaab03494ea3
        
        bios_proto_t *proto = bios_proto_decode (&message);        
        assert (proto);
        bios_proto_print (proto);
        
        data_t *data = data_new();

        //process data
        data_put (data, &proto);
        
        // give nonresponding devices
        zlistx_t *dead = zlistx_new();
        dead = data_get_dead (data);

        if (zlistx_size (dead))
        {
            printf("dead list not empty \n");
        }

        
        zlistx_destroy (&dead);
        data_destroy (&data);
        //bios_proto_destroy (&proto);
        
=======
        zsys_debug ("ELSE");
>>>>>>> Problem: no unit test for outage server
    }
    zpoller_destroy (&poller);
    // TODO: save/load the state
    data_destroy (&assets);
    zhash_destroy (&active_alerts);
    mlm_client_destroy (&client);
}


// --------------------------------------------------------------------------
// Self test of this class

void
bios_outage_server_test (bool verbose)
{
    printf (" * bios_outage_server: \n");

    //     @selftest
    static const char *endpoint =  "inproc://malamute-test2";

    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND",endpoint, NULL);
    zclock_sleep (1000);

    // malamute clients
    mlm_client_t *sender = mlm_client_new();
    int rv = mlm_client_connect (sender, endpoint, 5000, "sender");
    assert (rv >= 0);
    rv = mlm_client_set_producer (sender, "METRICS");
    assert (rv >= 0);

    mlm_client_t *consumer = mlm_client_new();
    rv = mlm_client_connect (consumer, endpoint, 5000, "alert-consumer");
    assert (rv >= 0);
    rv = mlm_client_set_consumer (consumer, "ALERTS", ".*");
    assert (rv >= 0);


    zactor_t *outsvr = zactor_new (bios_outage_server, (void*) NULL);
    assert (outsvr);

    //    actor commands
    zstr_sendx (outsvr, "ENDPOINT", endpoint, "outage-actor1", NULL);
    zstr_sendx (outsvr, "CONSUMER", "METRICS", ".*", NULL);
    zstr_sendx (outsvr, "CONSUMER", "_METRICS_SENSOR", ".*", NULL);
    //TODO: react on those messages to resolve alerts
    //zstr_sendx (outsvr, "CONSUMER", "_METRICS_UNAVAILABLE", ".*", NULL);
    zstr_sendx (outsvr, "PRODUCER", "ALERTS", NULL);

    //   set producer  test
    mlm_client_t *sender = mlm_client_new ();
    int rv = mlm_client_connect (sender, endpoint, 5000, "sender");
    assert (rv >= 0);

    rv = mlm_client_set_producer (sender, "xyz"); // "xyz = stream name"
    assert (rv >= 0);

    zmsg_t *sendmsg = bios_proto_encode_metric (
        NULL,
        "dev",
        "UPS33",
        "1",
        "c",
        0);

    zsys_debug ("sent metric");
    rv = mlm_client_send (sender, "subject",  &sendmsg);
    assert (rv >= 0);
    zclock_sleep (1000);

    zmsg_t *msg = mlm_client_recv (consumer);
    zmsg_print (msg);
    zmsg_destroy (&msg);

    mlm_client_destroy (&sender);
    mlm_client_destroy (&consumer);
    zactor_destroy(&outsvr);
    zactor_destroy (&server);
    
    //  @end
    printf ("OK\n");
}
