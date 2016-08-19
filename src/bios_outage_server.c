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

typedef struct _s_osrv_t {
    bool verbose;
    uint64_t timeout_ms;
    mlm_client_t *client;
    data_t *assets;
    zhash_t *active_alerts;
} s_osrv_t;

static s_osrv_t *
s_osrv_new () {
    s_osrv_t *self = (s_osrv_t*) zmalloc (sizeof (s_osrv_t));
    assert (self);

    self->verbose = false;
    self->timeout_ms = TIMEOUT;
    self->client = mlm_client_new ();
    assert (self->client);
    self->assets = data_new ();
    assert (self->assets);
    self->active_alerts = zhash_new ();
    assert (self->active_alerts);

    return self;
}

static void
s_osrv_destroy (s_osrv_t **self_p) {
    assert (self_p);
    if (*self_p) {
        s_osrv_t *self = *self_p;
        zhash_destroy (&self->active_alerts);
        data_destroy (&self->assets);
        mlm_client_destroy (&self->client);
        *self_p = NULL;
    }
}

static void
s_osrv_send_alert (s_osrv_t* self, const char* source, const char* state) {
    assert (self);
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
    mlm_client_send (self->client, subject, &msg);
    zstr_free (&subject);
}

/*
 * return values :
 * 1 - $TERM recieved
 * 0 - message processed and deleted
 */

static int
s_osrv_actor_commands (s_osrv_t* self, zmsg_t **message_p)
{
    assert (self);
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
            if (self->verbose)
                zsys_debug ("outage_actor: ENDPOINT: %s/%s", endpoint, name);
		    int rv = mlm_client_connect (self->client, endpoint, 1000, name);
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
            if (self->verbose)
                zsys_debug ("outage_actor: CONSUMER: %s/%s", stream, regex);
            int rv = mlm_client_set_consumer (self->client, stream, regex);
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
            if (self->verbose)
                zsys_debug ("outage_actor: PRODUCER: %s", stream);
            int rv = mlm_client_set_producer (self->client, stream);
            if (rv == -1 )
                zsys_error ("mlm_client_set_producer");
        }
        zstr_free(&stream);
    }
    else
    if (streq (command, "TIMEOUT"))
    {
        char *timeout = zmsg_popstr(message);

        if (timeout){
            self->timeout_ms = (uint64_t) atoll (timeout);
            if (self->verbose)
                zsys_debug ("outage_actor: TIMEOUT: \"%s\"/%"PRIu64, timeout, self->timeout_ms);
        }
        zstr_free(&timeout);
    }
    else
    if (streq (command, "VERBOSE"))
    {
        self->verbose = true;
        zsys_debug ("outage_actor: VERBOSE=true");
    }
	else {
        zsys_error ("outage_actor: Unknown actor command: %s.\n", command);
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

    s_osrv_t *self = s_osrv_new ();
    assert (self);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (self->client), NULL);
    assert(poller);

    zsock_signal (pipe, 0);

    //    poller timeout
    uint64_t now = zclock_mono ();
    uint64_t last_dead_check = now;

    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, self->timeout_ms);

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                zsys_info ("Terminating.");
                break;
            }
        }

        // send alerts
        now = zclock_mono ();
        if (zpoller_expired (poller) || (now - last_dead_check) > self->timeout_ms) {
            if (self->verbose)
                zsys_debug ("poller expired");
            zlistx_t *dead_devices = data_get_dead (self->assets);
            if (self->verbose)
                zsys_debug ("dead_devices.size=%zu", zlistx_size (dead_devices));
            for (void *it = zlistx_first (dead_devices);
                       it != NULL;
                       it = zlistx_next (dead_devices))
            {
                const char* source = (const char*) it;
                if (self->verbose)
                    zsys_debug ("\tsource=%s", source);
                if (!zhash_lookup (self->active_alerts, source)) {
                    if (self->verbose)
                        zsys_debug ("\t\tsend alert for source=%s", source);
                    s_osrv_send_alert (self, source, "ACTIVE");
                    zhash_insert (self->active_alerts, source, TRUE);
                }
                else
                    if (self->verbose)
                        zsys_debug ("\t\talert already active for source=%s", source);
            }
            zlistx_destroy (&dead_devices);
        }

        if (which == pipe) {
            if (self->verbose)
                zsys_debug ("which == pipe");
            zmsg_t *msg = zmsg_recv(pipe);
            if (!msg)
                break;

            int rv = s_osrv_actor_commands (self, &msg);
            if (rv == 1)
                break;
            continue;
        }
        // react on incoming messages
        else
        if (which == mlm_client_msgpipe (self->client)) {

            zmsg_t *message = mlm_client_recv (self->client);
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
                    if (zhash_lookup (self->active_alerts, source)) {
                        s_osrv_send_alert (self, source, "RESOLVED");
                        zhash_delete (self->active_alerts, source);
                    }
                }
                // add to cache
                if (self->verbose)
                    zsys_debug ("data_put");
                data_put (self->assets, &bmsg);
            }
            bios_proto_destroy (&bmsg);
            continue;
        }
        
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
    }
    zpoller_destroy (&poller);
    // TODO: save/load the state
    s_osrv_destroy (&self);
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
    zstr_sendx (outsvr, "VERBOSE", NULL);
    zstr_sendx (outsvr, "ENDPOINT", endpoint, "outage-actor1", NULL);
    zstr_sendx (outsvr, "CONSUMER", "METRICS", ".*", NULL);
    zstr_sendx (outsvr, "CONSUMER", "_METRICS_SENSOR", ".*", NULL);
    //TODO: react on those messages to resolve alerts
    //zstr_sendx (outsvr, "CONSUMER", "_METRICS_UNAVAILABLE", ".*", NULL);
    zstr_sendx (outsvr, "PRODUCER", "ALERTS", NULL);
    zstr_sendx (outsvr, "TIMEOUT", "1000", NULL);

    //   set producer  test
    mlm_client_t *sender = mlm_client_new ();
    int rv = mlm_client_connect (sender, endpoint, 5000, "sender");
    assert (rv >= 0);

    rv = mlm_client_set_producer (sender, "xyz"); // "xyz = stream name"
    assert (rv >= 0);
    //to give a time for all the clients and actors to initialize
    zclock_sleep (1000);

    // test case 01 to send the metric with short TTL
    // expected: ACTIVE alert to be sent
    zmsg_t *sendmsg = bios_proto_encode_metric (
        NULL,
        "dev",
        "UPS33",
        "1",
        "c",
        0);

    rv = mlm_client_send (sender, "subject",  &sendmsg);
    assert (rv >= 0);
    zclock_sleep (1000);

    zmsg_t *msg = mlm_client_recv (consumer);
    assert (msg);
    bios_proto_t *bmsg = bios_proto_decode (&msg);
    assert (bmsg);
    if (verbose)
        bios_proto_print (bmsg);
    assert (streq (bios_proto_element_src (bmsg), "UPS33"));
    assert (streq (bios_proto_state (bmsg), "ACTIVE"));
    bios_proto_destroy (&bmsg);

    // test case 02 to resolve alert by sending an another metric
    // expected: RESOLVED alert to be sent
    sendmsg = bios_proto_encode_metric (
        NULL,
        "dev",
        "UPS33",
        "1",
        "c",
        1000);

    rv = mlm_client_send (sender, "subject",  &sendmsg);
    assert (rv >= 0);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = bios_proto_decode (&msg);
    assert (bmsg);
    if (verbose)
        bios_proto_print (bmsg);
    assert (streq (bios_proto_element_src (bmsg), "UPS33"));
    assert (streq (bios_proto_state (bmsg), "RESOLVED"));
    bios_proto_destroy (&bmsg);

    zactor_destroy(&outsvr);
    mlm_client_destroy (&sender);
    mlm_client_destroy (&consumer);
    zactor_destroy (&server);
    
    //  @end
    printf ("OK\n");
}
