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
#define TIMEOUT_MS 30000   //wait at least 30 seconds

#include "agent_outage_classes.h"
#include "data.h"

typedef struct _s_osrv_t {
    bool verbose;
    uint64_t timeout_ms;
    mlm_client_t *client;
    data_t *assets;
    zhash_t *active_alerts;
} s_osrv_t;

static void
s_osrv_destroy (s_osrv_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        s_osrv_t *self = *self_p;
        zhash_destroy (&self->active_alerts);
        data_destroy (&self->assets);
        mlm_client_destroy (&self->client);
        free (self);
        *self_p = NULL;
    }
}

static s_osrv_t *
s_osrv_new ()
{
    s_osrv_t *self = (s_osrv_t*) zmalloc (sizeof (s_osrv_t));
    if (self) {
        self->client = mlm_client_new ();
        if (self->client) 
            self->assets = data_new ();
        if (self->assets)
            self->active_alerts = zhash_new ();
        if (self->active_alerts) {
            self->verbose = false;
            self->timeout_ms = TIMEOUT_MS;
        } else {
            s_osrv_destroy (&self);
        }
    }
    return self;
}

// publish 'outage' alert for asset 'source-asset' in state 'alert-state'
static void
s_osrv_send_alert (s_osrv_t* self, const char* source_asset, const char* alert_state)
{
    assert (self);
    assert (source_asset);
    assert (alert_state);

    zmsg_t *msg = bios_proto_encode_alert (
            NULL, // aux
            "outage", // rule_name
            source_asset,
            alert_state,
            "CRITICAL",
            "Device does not provide expected data. It may be offline or not correctly configured.",
            time (NULL),
            "EMAIL/SMS");
    char *subject = zsys_sprintf ("%s/%s@%s",
        "outage",
        "CRITICAL",
        source_asset);
    int rv = mlm_client_send (self->client, subject, &msg);
    if ( rv != 0 )
        zsys_error ("Cannot send alert on '%s' (mlm_cleint_send)", source_asset);
    zstr_free (&subject);
}

// if for asset 'source-asset' the 'outage' alert is tracked
// * publish alert in RESOLVE state for asset 'source-asset' 
// * removes alert from list of the active alerts
static void
s_osrv_resolve_alert (s_osrv_t* self, const char* source_asset)
{
    assert (self);
    assert (source_asset);

    if (zhash_lookup (self->active_alerts, source_asset)) {
        s_osrv_send_alert (self, source_asset, "RESOLVED");
        zhash_delete (self->active_alerts, source_asset);
    }
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
    if (streq(command, "CONNECT"))
    {
	    char *endpoint = zmsg_popstr (message);
		char *name = zmsg_popstr (message);
                
		if (endpoint && name) {
            if (self->verbose)
                zsys_debug ("outage_actor: CONNECT: %s/%s", endpoint, name);
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
    if (streq (command, "ASSET-EXPIRY-SEC"))
    {
        char *timeout = zmsg_popstr(message);

        if (timeout){
            data_set_default_expiry (self->assets, atol (timeout));
            if (self->verbose)
                zsys_debug ("outage_actor: ASSET-EXPIRY-SEC: \"%s\"/%"PRIu64, timeout, atol (timeout));
        }
        zstr_free(&timeout);
    }
    else
    if (streq (command, "VERBOSE"))
    {
        self->verbose = true;
        data_set_verbose (self->assets, true);
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
    zsys_info ("agent_outage: Started");
    //    poller timeout
    uint64_t now_ms = zclock_mono ();
    uint64_t last_dead_check_ms = now_ms;

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
        now_ms = zclock_mono ();
        if (zpoller_expired (poller) || (now_ms - last_dead_check_ms) > self->timeout_ms) {
            if (self->verbose)
                zsys_debug ("poll event");
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
            last_dead_check_ms = zclock_mono ();
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
                if (streq (mlm_client_address (self->client), BIOS_PROTO_STREAM_METRICS_UNAVAILABLE)) {
                    char *foo = zmsg_popstr (message);
                    if ( foo && streq (foo, "METRICUNAVAILABLE")) {
                        zstr_free (&foo);
                        foo = zmsg_popstr (message); // topic in form aaaa@bbb
                        const char* source = strstr (foo, "@") + 1;
                        s_osrv_resolve_alert (self, source);
                        data_delete (self->assets, source);
                    }
                    zstr_free (&foo);
                }
                zmsg_destroy(&message);
                continue;
            }

            bios_proto_t *bmsg = bios_proto_decode (&message);
            if (bmsg) {
                // resolve sent alert
                if (bios_proto_id (bmsg) == BIOS_PROTO_METRIC) {
                    const char *is_computed = bios_proto_aux_string (bmsg, "x-cm-count", NULL);
                    if ( !is_computed ) {
                        const char *source = bios_proto_element_src (bmsg);
                        s_osrv_resolve_alert (self, source);
                        data_put (self->assets, bmsg);
                    }
                    else {
                        // intentionally left empty
                        // so it is metric from agent-cm -> it is not comming from the dvice itself ->ignore it
                    }
                }
                else
                if (bios_proto_id (bmsg) == BIOS_PROTO_ASSET) {
                    const char* source = bios_proto_name (bmsg);
                    s_osrv_resolve_alert (self, source);
                    data_put (self->assets, bmsg);
                }
            }
            bios_proto_destroy (&bmsg);
        }
    }
    zpoller_destroy (&poller);
    // TODO: save/load the state
    s_osrv_destroy (&self);
    zsys_info ("agent_outage: Ended");
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
    mlm_client_t *m_sender = mlm_client_new();
    int rv = mlm_client_connect (m_sender, endpoint, 5000, "m_sender");
    assert (rv >= 0);
    rv = mlm_client_set_producer (m_sender, "METRICS");
    assert (rv >= 0);
    
    mlm_client_t *a_sender = mlm_client_new();
    rv = mlm_client_connect (a_sender, endpoint, 5000, "a_sender");
    assert (rv >= 0);
    rv = mlm_client_set_producer (a_sender, "ASSETS");
    assert (rv >= 0);

    mlm_client_t *consumer = mlm_client_new();
    rv = mlm_client_connect (consumer, endpoint, 5000, "alert-consumer");
    assert (rv >= 0);
    rv = mlm_client_set_consumer (consumer, "_ALERTS_SYS", ".*");
    assert (rv >= 0);

    zactor_t *self = zactor_new (bios_outage_server, (void*) NULL);
    assert (self);

    //    actor commands
    if (verbose)
        zstr_sendx (self, "VERBOSE", NULL);
    zstr_sendx (self, "CONNECT", endpoint, "outage-actor1", NULL);
    zstr_sendx (self, "CONSUMER", "METRICS", ".*", NULL);
    zstr_sendx (self, "CONSUMER", "ASSETS", ".*", NULL);
    zstr_sendx (self, "CONSUMER", "_METRICS_SENSOR", ".*", NULL);
    zstr_sendx (self, "CONSUMER", "_METRICS_UNAVAILABLE", ".*", NULL);
    zstr_sendx (self, "PRODUCER", "_ALERTS_SYS", NULL);
    zstr_sendx (self, "TIMEOUT", "1000", NULL);
    zstr_sendx (self, "ASSET-EXPIRY-SEC", "3", NULL);

    //to give a time for all the clients and actors to initialize
    zclock_sleep (1000);

    // test case 01 to send the metric with short TTL
    zhash_t *asset_aux = zhash_new ();
    zhash_insert (asset_aux, "type", "device");
    zhash_insert (asset_aux, "subtype", "ups");
    zmsg_t *sendmsg = bios_proto_encode_asset (asset_aux, "UPS33", "create", NULL);
    zhash_destroy (&asset_aux);
    rv = mlm_client_send (a_sender, "subject",  &sendmsg);

    // expected: ACTIVE alert to be sent
    sendmsg = bios_proto_encode_metric (
        NULL,
        "dev",
        "UPS33",
        "1",
        "c",
        1);

    rv = mlm_client_send (m_sender, "subject",  &sendmsg);
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

    rv = mlm_client_send (m_sender, "subject",  &sendmsg);
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

    //  cleanup from test case 02 - delete asset from cache
    sendmsg = bios_proto_encode_asset (
        NULL,
        "UPS33",
        BIOS_PROTO_ASSET_OP_DELETE,
        NULL);
    rv = mlm_client_send (m_sender, "subject",  &sendmsg);
    assert (rv >= 0);

    // test case 03: add new asset device, wait expiry time and check the alert
    zhash_t *aux = zhash_new ();
    zhash_insert (aux, BIOS_PROTO_ASSET_TYPE, "device");
    zhash_insert (aux, BIOS_PROTO_ASSET_SUBTYPE, "ups");
    zhash_insert (aux, BIOS_PROTO_ASSET_STATUS, "active");
    sendmsg = bios_proto_encode_asset (
        aux,
        "UPS42",
        BIOS_PROTO_ASSET_OP_CREATE,
        NULL);
    zhash_destroy (&aux);
    rv = mlm_client_send (m_sender, "UPS42",  &sendmsg);
    assert (rv >= 0);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = bios_proto_decode (&msg);
    assert (bmsg);
    if (verbose)
        bios_proto_print (bmsg);
    assert (streq (bios_proto_element_src (bmsg), "UPS42"));
    assert (streq (bios_proto_state (bmsg), "ACTIVE"));
    bios_proto_destroy (&bmsg);

    // test case 04: RESOLVE alert when device is retired
    aux = zhash_new ();
    zhash_insert (aux, BIOS_PROTO_ASSET_TYPE, "device");
    zhash_insert (aux, BIOS_PROTO_ASSET_SUBTYPE, "ups");
    zhash_insert (aux, BIOS_PROTO_ASSET_STATUS, "retired");
    sendmsg = bios_proto_encode_asset (
        aux,
        "UPS42",
        BIOS_PROTO_ASSET_OP_UPDATE,
        NULL);
    zhash_destroy (&aux);
    rv = mlm_client_send (m_sender, "UPS42",  &sendmsg);
    assert (rv >= 0);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = bios_proto_decode (&msg);
    assert (bmsg);
    if (verbose)
        bios_proto_print (bmsg);
    assert (streq (bios_proto_element_src (bmsg), "UPS42"));
    assert (streq (bios_proto_state (bmsg), "RESOLVED"));
    bios_proto_destroy (&bmsg);

    zactor_destroy(&self);
    mlm_client_destroy (&m_sender);
    mlm_client_destroy (&a_sender);
    mlm_client_destroy (&consumer);
    zactor_destroy (&server);
    
    //  @end
    printf ("OK\n");
}
