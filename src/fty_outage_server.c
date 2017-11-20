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
#define TIMEOUT_MS 30000   //wait at least 30 seconds
#define SAVE_INTERVAL_MS 45*60*1000 // store state each 45 minutes

#include "fty_outage_classes.h"
#include "data.h"

static void *TRUE = (void*) "true";   // hack to allow us to pretend zhash is set

typedef struct _s_osrv_t {
    bool verbose;
    uint64_t timeout_ms;
    mlm_client_t *client;
    data_t *assets;
    zhash_t *active_alerts;
    char *state_file;
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
        zstr_free (&self->state_file);
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
            self->state_file = NULL;
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

    zlist_t *actions = zlist_new ();
    zlist_append(actions, "EMAIL");
    zlist_append(actions, "SMS");
    zmsg_t *msg = fty_proto_encode_alert (
            NULL, // aux
            zclock_time() / 1000,
            self->timeout_ms * 3,
            "outage", // rule_name
            source_asset,
            alert_state,
            "CRITICAL",
            "Device does not provide expected data. It may be offline or not correctly configured.",
            actions);
    char *subject = zsys_sprintf ("%s/%s@%s",
        "outage",
        "CRITICAL",
        source_asset);
    if ( self->verbose )
        zsys_debug ("Alert '%s' is '%s'", subject, alert_state);
    int rv = mlm_client_send (self->client, subject, &msg);
    if ( rv != 0 )
        zsys_error ("Cannot send alert on '%s' (mlm_client_send)", source_asset);
    zlist_destroy(&actions);
    zstr_free (&subject);
}

// if for asset 'source-asset' the 'outage' alert is tracked
// * publish alert in RESOLVE state for asset 'source-asset'
// * removes alert from the list of the active alerts
static void
s_osrv_resolve_alert (s_osrv_t* self, const char* source_asset)
{
    assert (self);
    assert (source_asset);

    if (zhash_lookup (self->active_alerts, source_asset)) {
        if (self->verbose)
            zsys_debug ("\t\tsend RESOLVED alert for source=%s", source_asset);
        s_osrv_send_alert (self, source_asset, "RESOLVED");
        zhash_delete (self->active_alerts, source_asset);
    }
}

// if for asset 'source-asset' the 'outage' alert is NOT tracked
// * publish alert in ACTIVE state for asset 'source-asset'
// * adds alert to the list of the active alerts
static void
s_osrv_activate_alert (s_osrv_t* self, const char* source_asset)
{
    assert (self);
    assert (source_asset);

    if ( !zhash_lookup (self->active_alerts, source_asset)) {
        if (self->verbose)
            zsys_debug ("\t\tsend ACTIVE alert for source=%s", source_asset);
        s_osrv_send_alert (self, source_asset, "ACTIVE");
        zhash_insert (self->active_alerts, source_asset, TRUE);
    }
    else
        if (self->verbose)
            zsys_debug ("\t\talert already active for source=%s", source_asset);
}

static int
s_osrv_save (s_osrv_t *self)
{
    assert (self);

    if (!self->state_file) {
        zsys_warning ("There is no state path set-up, can't store the state");
        return -1;
    }

    zconfig_t *root = zconfig_new ("root", NULL);
    assert (root);

    zconfig_t *active_alerts = zconfig_new ("alerts", root);
    assert (active_alerts);

    size_t i = 0;
    for (void*  it = zhash_first (self->active_alerts);
                it != NULL;
                it = zhash_next (self->active_alerts))
    {
        const char *value = (const char*) zhash_cursor (self->active_alerts);
        char *key = zsys_sprintf ("%zu", i++);
        zconfig_put (active_alerts, key, value);
        zstr_free (&key);
    }

    int ret = zconfig_save (root, self->state_file);
    if (self->verbose)
        zsys_debug ("outage_actor: save state to %s", self->state_file);
    zconfig_destroy (&root);
    return ret;
}

static int
s_osrv_load (s_osrv_t *self)
{
    assert (self);

    if (!self->state_file) {
        zsys_warning ("There is no state path set-up, can't load the state");
        return -1;
    }

    zconfig_t *root = zconfig_load (self->state_file);
    if (!root) {
        zsys_error ("Can't load configuration from %s: %m", self->state_file);
        return -1;
    }

    zconfig_t *active_alerts = zconfig_locate (root, "alerts");
    if (!active_alerts) {
        zsys_error ("Can't find 'alerts' in %s", self->state_file);
        zconfig_destroy (&root);
        return -1;
    }

    for (zconfig_t *child = zconfig_child (active_alerts);
                    child != NULL;
                    child = zconfig_next (child))
    {
        zhash_insert (self->active_alerts, zconfig_value (child), TRUE);
    }

    zconfig_destroy (&root);
    return 0;
}

static void
s_osrv_check_dead_devices (s_osrv_t *self)
{
    assert (self);

    if (self->verbose)
        zsys_debug ("time to check dead devices");
    zlistx_t *dead_devices = data_get_dead (self->assets);
    if ( !dead_devices ) {
        zsys_error ("Can't get a list of dead devices (memory error)");
        return;
    }
    if (self->verbose)
        zsys_debug ("dead_devices.size=%zu", zlistx_size (dead_devices));
    for (void *it = zlistx_first (dead_devices);
            it != NULL;
            it = zlistx_next (dead_devices))
    {
        const char* source = (const char*) it;
        if (self->verbose)
            zsys_debug ("\tsource=%s", source);
        s_osrv_activate_alert (self, source);
    }
    zlistx_destroy (&dead_devices);
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

        if (timeout) {
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
    if (streq (command, "STATE-FILE"))
    {
        char *state_file = zmsg_popstr(message);
        if (state_file) {
            self->state_file = strdup (state_file);
            if (self->verbose)
                zsys_debug ("outage_actor: STATE-FILE: %s", state_file);
            int r = s_osrv_load (self);
            if (r != 0)
                zsys_error ("outage_actor: failed to load state file %s: %m", self->state_file);
        }
        zstr_free(&state_file);
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
// Create a new fty_outage_server
void
fty_outage_server (zsock_t *pipe, void *args)
{
    s_osrv_t *self = s_osrv_new ();
    assert (self);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (self->client), NULL);
    assert (poller);

    zsock_signal (pipe, 0);
    zsys_info ("agent_outage: Started");
    //    poller timeout
    uint64_t now_ms = zclock_mono ();
    uint64_t last_dead_check_ms = now_ms;
    uint64_t last_save_ms = now_ms;

    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, self->timeout_ms);

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                zsys_info ("Terminating.");
                break;
            }
        }

        now_ms = zclock_mono ();

        // save the state
        if ((now_ms - last_save_ms) > SAVE_INTERVAL_MS) {
            int r = s_osrv_save (self);
            if (r != 0)
                zsys_error ("outage_actor: failed to save state file %s: %m", self->state_file);
            last_save_ms = now_ms;
        }

        // send alerts
        if (zpoller_expired (poller) || (now_ms - last_dead_check_ms) > self->timeout_ms) {
            s_osrv_check_dead_devices (self);
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

            if (!is_fty_proto(message)) {
                if (streq (mlm_client_address (self->client), FTY_PROTO_STREAM_METRICS_UNAVAILABLE)) {
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

            fty_proto_t *bmsg = fty_proto_decode (&message);
            if (!bmsg)
                continue;

            // resolve sent alert
            if (fty_proto_id (bmsg) == FTY_PROTO_METRIC || streq (mlm_client_address (self->client), FTY_PROTO_STREAM_METRICS_SENSOR)) {
                const char *is_computed = fty_proto_aux_string (bmsg, "x-cm-count", NULL);
                if ( !is_computed ) {
                    uint64_t now_sec = zclock_time() / 1000;
                    uint64_t timestamp = fty_proto_time (bmsg);
                    const char* port = fty_proto_aux_string (bmsg, FTY_PROTO_METRICS_SENSOR_AUX_PORT, NULL);

                    if (port != NULL ) {
                        // is it from sensor? yes
                        // get sensors attached to the 'asset' on the 'port'! we can have more then 1!
                        const char *source = fty_proto_aux_string (bmsg, FTY_PROTO_METRICS_SENSOR_AUX_SNAME, NULL);
                        if (NULL == source) {
                            zsys_error("Sensor message malformed: found %s='%s' but %s is missing", FTY_PROTO_METRICS_SENSOR_AUX_PORT,
                                    port, FTY_PROTO_METRICS_SENSOR_AUX_SNAME);
                            continue;
                        }
                        if ( self->verbose ) {
                            zsys_debug ("Sensor '%s' on '%s'/'%s' is still alive", source,  fty_proto_name (bmsg), port);
                        }
                        s_osrv_resolve_alert (self, source);
                        int rv = data_touch_asset (self->assets, source, timestamp, fty_proto_ttl (bmsg), now_sec);
                        if ( rv == -1 )
                            zsys_error ("asset: name = %s, topic=%s metric is from future! ignore it", source, mlm_client_subject (self->client));
                    }
                    else {
                        // is it from sensor? no
                        const char *source = fty_proto_name (bmsg);
                        s_osrv_resolve_alert (self, source);
                        int rv = data_touch_asset (self->assets, source, timestamp, fty_proto_ttl (bmsg), now_sec);
                        if ( rv == -1 )
                            zsys_error ("asset: name = %s, topic=%s metric is from future! ignore it", source, mlm_client_subject (self->client));
                    }
                }
                else {
                    // intentionally left empty
                    // so it is metric from agent-cm -> it is not comming from the device itself ->ignore it
                }
            }
            else
            if (fty_proto_id (bmsg) == FTY_PROTO_ASSET) {
                if (    streq (fty_proto_operation (bmsg), FTY_PROTO_ASSET_OP_DELETE)
                     || streq (fty_proto_aux_string (bmsg, FTY_PROTO_ASSET_STATUS, ""), "retired") )
                {
                    const char* source = fty_proto_name (bmsg);
                    s_osrv_resolve_alert (self, source);
                }
                data_put (self->assets, &bmsg);
            }
            fty_proto_destroy (&bmsg);
        }
    }
    zpoller_destroy (&poller);
    int r = s_osrv_save (self);
    if (r != 0)
        zsys_error ("outage_actor: failed to save state file %s: %m", self->state_file);
    s_osrv_destroy (&self);
    zsys_info ("agent_outage: Ended");
}

// --------------------------------------------------------------------------
// Self test of this class

void
fty_outage_server_test (bool verbose)
{
    printf (" * fty_outage_server: \n");

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

    zactor_t *self = zactor_new (fty_outage_server, (void*) NULL);
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
    zmsg_t *sendmsg = fty_proto_encode_asset (asset_aux, "UPS33", "create", NULL);
    zhash_destroy (&asset_aux);
    rv = mlm_client_send (a_sender, "subject",  &sendmsg);

    // expected: ACTIVE alert to be sent
    sendmsg = fty_proto_encode_metric (
        NULL,
        time (NULL),
        1,
        "dev",
        "UPS33",
        "1",
        "c");

    rv = mlm_client_send (m_sender, "subject",  &sendmsg);
    assert (rv >= 0);
    zclock_sleep (1000);

    zmsg_t *msg = mlm_client_recv (consumer);
    assert (msg);
    fty_proto_t *bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    if (verbose)
        fty_proto_print (bmsg);
    assert (streq (fty_proto_name (bmsg), "UPS33"));
    assert (streq (fty_proto_state (bmsg), "ACTIVE"));
    fty_proto_destroy (&bmsg);

    // test case 02 to resolve alert by sending an another metric
    // expected: RESOLVED alert to be sent
    sendmsg = fty_proto_encode_metric (
        NULL,
        time (NULL),
        1000,
        "dev",
        "UPS33",
        "1",
        "c");

    rv = mlm_client_send (m_sender, "subject",  &sendmsg);
    assert (rv >= 0);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    if (verbose)
        fty_proto_print (bmsg);
    assert (streq (fty_proto_name (bmsg), "UPS33"));
    assert (streq (fty_proto_state (bmsg), "RESOLVED"));
    fty_proto_destroy (&bmsg);

    //  cleanup from test case 02 - delete asset from cache
    sendmsg = fty_proto_encode_asset (
        NULL,
        "UPS33",
        FTY_PROTO_ASSET_OP_DELETE,
        NULL);
    rv = mlm_client_send (m_sender, "subject",  &sendmsg);
    assert (rv >= 0);

    // test case 03: add new asset device, wait expiry time and check the alert
    zhash_t *aux = zhash_new ();
    zhash_insert (aux, FTY_PROTO_ASSET_TYPE, "device");
    zhash_insert (aux, FTY_PROTO_ASSET_SUBTYPE, "ups");
    zhash_insert (aux, FTY_PROTO_ASSET_STATUS, "active");
    sendmsg = fty_proto_encode_asset (
        aux,
        "UPS42",
        FTY_PROTO_ASSET_OP_CREATE,
        NULL);
    zhash_destroy (&aux);
    rv = mlm_client_send (m_sender, "UPS42",  &sendmsg);
    assert (rv >= 0);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    if (verbose)
        fty_proto_print (bmsg);
    assert (streq (fty_proto_name (bmsg), "UPS42"));
    assert (streq (fty_proto_state (bmsg), "ACTIVE"));
    fty_proto_destroy (&bmsg);

    // test case 04: RESOLVE alert when device is retired
    aux = zhash_new ();
    zhash_insert (aux, FTY_PROTO_ASSET_TYPE, "device");
    zhash_insert (aux, FTY_PROTO_ASSET_SUBTYPE, "ups");
    zhash_insert (aux, FTY_PROTO_ASSET_STATUS, "retired");
    sendmsg = fty_proto_encode_asset (
        aux,
        "UPS42",
        FTY_PROTO_ASSET_OP_UPDATE,
        NULL);
    zhash_destroy (&aux);
    rv = mlm_client_send (m_sender, "UPS42",  &sendmsg);
    assert (rv >= 0);

    msg = mlm_client_recv (consumer);
    assert (msg);
    bmsg = fty_proto_decode (&msg);
    assert (bmsg);
    if (verbose)
        fty_proto_print (bmsg);
    assert (streq (fty_proto_name (bmsg), "UPS42"));
    assert (streq (fty_proto_state (bmsg), "RESOLVED"));
    fty_proto_destroy (&bmsg);

    zactor_destroy(&self);
    mlm_client_destroy (&m_sender);
    mlm_client_destroy (&a_sender);
    mlm_client_destroy (&consumer);
    zactor_destroy (&server);

    //  @end

    // Those are PRIVATE to actor, so won't be a part of documentation
    s_osrv_t * self2 = s_osrv_new ();
    zhash_insert (self2->active_alerts, "DEVICE1", TRUE);
    zhash_insert (self2->active_alerts, "DEVICE2", TRUE);
    zhash_insert (self2->active_alerts, "DEVICE3", TRUE);
    zhash_insert (self2->active_alerts, "DEVICE WITH SPACE", TRUE);
    self2->state_file = strdup ("src/state.zpl");
    s_osrv_save (self2);
    s_osrv_destroy (&self2);

    self2 = s_osrv_new ();
    self2->state_file = strdup ("src/state.zpl");
    s_osrv_load (self2);

    assert (zhash_size (self2->active_alerts) == 4);
    assert (zhash_lookup (self2->active_alerts, "DEVICE1"));
    assert (zhash_lookup (self2->active_alerts, "DEVICE2"));
    assert (zhash_lookup (self2->active_alerts, "DEVICE3"));
    assert (zhash_lookup (self2->active_alerts, "DEVICE WITH SPACE"));
    assert (!zhash_lookup (self2->active_alerts, "DEVICE4"));

    s_osrv_destroy (&self2);

    unlink ("src/state.zpl");
    printf ("OK\n");
}
