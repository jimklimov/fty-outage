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
#include "fty_common_macros.h"

static void *TRUE = (void*) "true";   // hack to allow us to pretend zhash is set

typedef struct _s_osrv_t {
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
    zlist_append(actions, (void *) "EMAIL");
    zlist_append(actions, (void *) "SMS");
    char *rule_name = zsys_sprintf ("%s@%s","outage",source_asset);
    std::string description = TRANSLATE_ME("Device %s does not provide expected data. It may be offline or not correctly configured.", data_get_asset_ename (self->assets, source_asset));
    zmsg_t *msg = fty_proto_encode_alert (
            NULL, // aux
            zclock_time() / 1000,
            self->timeout_ms * 3,
            rule_name, // rule_name
            source_asset,
            alert_state,
            "CRITICAL",
            description.c_str(),
            actions);
    char *subject = zsys_sprintf ("%s/%s@%s",
        "outage",
        "CRITICAL",
        source_asset);
    log_debug ("Alert '%s' is '%s'", subject, alert_state);
    int rv = mlm_client_send (self->client, subject, &msg);
    if ( rv != 0 )
        log_error ("Cannot send alert on '%s' (mlm_client_send)", source_asset);
    zlist_destroy(&actions);
    zstr_free (&subject);
    zstr_free (&rule_name);
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
        log_info ("\t\tsend RESOLVED alert for source=%s", source_asset);
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
        log_info ("\t\tsend ACTIVE alert for source=%s", source_asset);
        s_osrv_send_alert (self, source_asset, "ACTIVE");
        zhash_insert (self->active_alerts, source_asset, TRUE);
    }
    else
        log_debug ("\t\talert already active for source=%s", source_asset);
}

static int
s_osrv_save (s_osrv_t *self)
{
    assert (self);

    if (!self->state_file) {
        log_warning ("There is no state path set-up, can't store the state");
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
    log_debug ("outage_actor: save state to %s", self->state_file);
    zconfig_destroy (&root);
    return ret;
}

static int
s_osrv_load (s_osrv_t *self)
{
    assert (self);

    if (!self->state_file) {
        log_warning ("There is no state path set-up, can't load the state");
        return -1;
    }

    zconfig_t *root = zconfig_load (self->state_file);
    if (!root) {
        log_error ("Can't load configuration from %s: %m", self->state_file);
        return -1;
    }

    zconfig_t *active_alerts = zconfig_locate (root, "alerts");
    if (!active_alerts) {
        log_error ("Can't find 'alerts' in %s", self->state_file);
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

    log_debug ("time to check dead devices");
    zlistx_t *dead_devices = data_get_dead (self->assets);
    if ( !dead_devices ) {
        log_error ("Can't get a list of dead devices (memory error)");
        return;
    }
    log_debug ("dead_devices.size=%zu", zlistx_size (dead_devices));
    for (void *it = zlistx_first (dead_devices);
            it != NULL;
            it = zlistx_next (dead_devices))
    {
        const char* source = (const char*) it;
        log_debug ("\tsource=%s", source);
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
        log_warning ("Empty command.");
        return 0;
    }
    log_debug("Command : %s",command);
    if (streq(command, "$TERM")) {
        log_debug ("Got $TERM");
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
                    log_debug ("outage_actor: CONNECT: %s/%s", endpoint, name);
		    int rv = mlm_client_connect (self->client, endpoint, 1000, name);
            if (rv == -1)
			    log_error("mlm_client_connect failed\n");
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
            log_debug ("CONSUMER: %s/%s", stream, regex);
            int rv = mlm_client_set_consumer (self->client, stream, regex);
            if (rv == -1 )
                log_error("mlm_set_consumer failed");
        }

        zstr_free (&stream);
        zstr_free (&regex);
    }
    else
    if (streq (command, "PRODUCER"))
    {
        char *stream = zmsg_popstr(message);

        if (stream){
            log_debug ("PRODUCER: %s", stream);
            int rv = mlm_client_set_producer (self->client, stream);
            if (rv == -1 )
                log_error ("mlm_client_set_producer");
        }
        zstr_free(&stream);
    }
    else
    if (streq (command, "TIMEOUT"))
    {
        char *timeout = zmsg_popstr(message);

        if (timeout) {
            self->timeout_ms = (uint64_t) atoll (timeout);
            log_debug ("TIMEOUT: \"%s\"/%" PRIu64, timeout, self->timeout_ms);
        }
        zstr_free(&timeout);
    }
    else
    if (streq (command, "ASSET-EXPIRY-SEC"))
    {
        char *timeout = zmsg_popstr(message);

        if (timeout){
            data_set_default_expiry (self->assets, atol (timeout));
            log_debug ("ASSET-EXPIRY-SEC: \"%s\"/%" PRIu64, timeout, atol (timeout));
        }
        zstr_free(&timeout);
    }
    else
    if (streq (command, "STATE-FILE"))
    {
        char *state_file = zmsg_popstr(message);
        if (state_file) {
            self->state_file = strdup (state_file);
            log_debug ("STATE-FILE: %s", state_file);
            int r = s_osrv_load (self);
            if (r != 0)
                log_error ("failed to load state file %s: %m", self->state_file);
        }
        zstr_free(&state_file);
    }
    else {
        log_error ("Unknown actor command: %s.\n", command);
    }

    zstr_free (&command);
    zmsg_destroy (message_p);
    return 0;
}

void
metric_processing (fty::shm::shmMetrics& metrics, void* args) {
  
  s_osrv_t *self = (s_osrv_t *) args;

  for (auto &element : metrics) {
    const char *is_computed = fty_proto_aux_string (element, "x-cm-count", NULL);
    if ( !is_computed ) {
        uint64_t now_sec = zclock_time() / 1000;
        uint64_t timestamp = fty_proto_time (element);
        const char* port = fty_proto_aux_string (element, FTY_PROTO_METRICS_SENSOR_AUX_PORT, NULL);

        if (port != NULL ) {
            // is it from sensor? yes
            // get sensors attached to the 'asset' on the 'port'! we can have more then 1!
            const char *source = fty_proto_aux_string (element, FTY_PROTO_METRICS_SENSOR_AUX_SNAME, NULL);
            if (NULL == source) {
                log_error("Sensor message malformed: found %s='%s' but %s is missing", FTY_PROTO_METRICS_SENSOR_AUX_PORT,
                        port, FTY_PROTO_METRICS_SENSOR_AUX_SNAME);
                continue;
            }
            log_debug ("Sensor '%s' on '%s'/'%s' is still alive", source,  fty_proto_name (element), port);
            s_osrv_resolve_alert (self, source);
            int rv = data_touch_asset (self->assets, source, timestamp, fty_proto_ttl (element), now_sec);
            if ( rv == -1 )
                log_error ("asset: name = %s, topic=%s metric is from future! ignore it", source, mlm_client_subject (self->client));
        }
        else {
            // is it from sensor? no
            const char *source = fty_proto_name (element);
            s_osrv_resolve_alert (self, source);
            int rv = data_touch_asset (self->assets, source, timestamp, fty_proto_ttl (element), now_sec);
            if ( rv == -1 )
                log_error ("asset: name = %s, topic=%s metric is from future! ignore it", source, mlm_client_subject (self->client));
        }
    }
    else {
        // intentionally left empty
        // so it is metric from agent-cm -> it is not comming from the device itself ->ignore it
    }
  }
}

void
outage_metric_polling (zsock_t *pipe, void *args)
{
  zpoller_t *poller = zpoller_new (pipe, NULL);
  zsock_signal (pipe, 0);

  while (!zsys_interrupted)
  {
      void *which = zpoller_wait (poller, fty_get_polling_interval() * 1000);
      if (zpoller_terminated(poller) || zsys_interrupted) {
          log_info ("outage_actor: Terminating.");
          break;
      }
      if (zpoller_expired (poller)) {
        fty::shm::shmMetrics result;
        log_debug("read metrics");
        fty::shm::read_metrics(".*", ".*", result);
        log_debug("i have read %d metric", result.size());
        metric_processing(result, args);
      }
      if (which == pipe) {
      zmsg_t *msg = zmsg_recv (pipe);
        if (msg) {
            char *cmd = zmsg_popstr (msg);
            if (cmd) {
                if (streq (cmd, "$TERM")) {
                    zstr_free (&cmd);
                    zmsg_destroy (&msg);
                    break;
                }
                zstr_free (&cmd);
            }
            zmsg_destroy (&msg);
        }
      }
      
  }
  zpoller_destroy(&poller);
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
    log_info ("outage_actor: Started");
    //    poller timeout
    uint64_t now_ms = zclock_mono ();
    uint64_t last_dead_check_ms = now_ms;
    uint64_t last_save_ms = now_ms;

    zactor_t *metric_poll = zactor_new(outage_metric_polling, (void*) self);
    while (!zsys_interrupted)
    {
        self->timeout_ms = fty_get_polling_interval() * 1000;
        void *which = zpoller_wait (poller, self->timeout_ms);

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                log_info ("outage_actor: Terminating.");
                break;
            }
        }

        now_ms = zclock_mono ();

        // save the state
        if ((now_ms - last_save_ms) > SAVE_INTERVAL_MS) {
            int r = s_osrv_save (self);
            if (r != 0)
                log_error ("failed to save state file %s", self->state_file);
            last_save_ms = now_ms;
        }

        // send alerts
        if (zpoller_expired (poller) || (now_ms - last_dead_check_ms) > self->timeout_ms) {
            s_osrv_check_dead_devices (self);
            last_dead_check_ms = zclock_mono ();
        }

        if (which == pipe) {
            log_trace ("which == pipe");
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
                            log_error("Sensor message malformed: found %s='%s' but %s is missing", FTY_PROTO_METRICS_SENSOR_AUX_PORT,
                                    port, FTY_PROTO_METRICS_SENSOR_AUX_SNAME);
                            continue;
                        }
                        log_debug ("Sensor '%s' on '%s'/'%s' is still alive", source,  fty_proto_name (bmsg), port);
                        s_osrv_resolve_alert (self, source);
                        int rv = data_touch_asset (self->assets, source, timestamp, fty_proto_ttl (bmsg), now_sec);
                        if ( rv == -1 )
                            log_error ("asset: name = %s, topic=%s metric is from future! ignore it", source, mlm_client_subject (self->client));
                    }
                    else {
                        // is it from sensor? no
                        const char *source = fty_proto_name (bmsg);
                        s_osrv_resolve_alert (self, source);
                        int rv = data_touch_asset (self->assets, source, timestamp, fty_proto_ttl (bmsg), now_sec);
                        if ( rv == -1 )
                            log_error ("asset: name = %s, topic=%s metric is from future! ignore it", source, mlm_client_subject (self->client));
                    }
                }
                else {
                    // intentionally left empty
                    // so it is metric from agent-cm -> it is not comming from the device itself ->ignore it
                }
            }
            else
            if (fty_proto_id (bmsg) == FTY_PROTO_ASSET) {
                if (streq (fty_proto_operation (bmsg), FTY_PROTO_ASSET_OP_DELETE)
                     || !streq (fty_proto_aux_string (bmsg, FTY_PROTO_ASSET_STATUS, "active"), "active") )
                {
                    const char* source = fty_proto_name (bmsg);
                    s_osrv_resolve_alert (self, source);
                }
                data_put (self->assets, &bmsg);
            }
            fty_proto_destroy (&bmsg);
        }
    }
    zactor_destroy (&metric_poll);
    zpoller_destroy (&poller);
    int r = s_osrv_save (self);
    if (r != 0)
        log_error ("outage_actor: failed to save state file %s: %m", self->state_file);
    s_osrv_destroy (&self);
    log_info ("outage_actor: Ended");
}

// --------------------------------------------------------------------------
// Self test of this class

void
fty_outage_server_test (bool verbose)
{
    printf (" * fty_outage_server: \n");
    ftylog_setInstance("fty_outage_server_test","");
    if (verbose)
        ftylog_setVeboseMode(ftylog_getInstance());
    //     @selftest
    static const char *endpoint =  "inproc://malamute-test2";

    zactor_t *server = zactor_new (mlm_server, (void*) "Malamute");
    zstr_sendx (server, "BIND",endpoint, NULL);

    // malamute clients
//    mlm_client_t *m_sender = mlm_client_new();
//    int rv = mlm_client_connect (m_sender, endpoint, 5000, "m_sender");
//    assert (rv >= 0);
//    rv = mlm_client_set_producer (m_sender, "METRICS");
//    assert (rv >= 0);

    int polling_value = 10;
    int wanted_ttl = 2*polling_value-1;
    fty_shm_set_default_polling_interval(polling_value);
    assert(fty_shm_set_test_dir("src/selftest-rw") == 0);

    mlm_client_t *a_sender = mlm_client_new();
    int rv = mlm_client_connect (a_sender, endpoint, 5000, "a_sender");
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
    zhash_t *asset_ext = zhash_new ();
    zhash_insert (asset_ext, "name", (void *) "ename_of_ups33");
    zhash_t *asset_aux = zhash_new ();
    zhash_insert (asset_aux, "type", (void *) "device");
    zhash_insert (asset_aux, "subtype", (void *) "ups");
    zmsg_t *sendmsg = fty_proto_encode_asset (asset_aux, "UPS33", "create", asset_ext);
    zhash_destroy (&asset_aux);
    zhash_destroy (&asset_ext);

    rv = mlm_client_send (a_sender, "subject",  &sendmsg);

    // expected: ACTIVE alert to be sent
//    sendmsg = fty_proto_encode_metric (
//        NULL,
//        time (NULL),
//        1,
//        "dev",
//        "UPS33",
//        "1",
//        "c");
//
//    rv = mlm_client_send (m_sender, "subject",  &sendmsg);
    rv = fty::shm::write_metric("UPS33", "dev", "1", "c", wanted_ttl);
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
//    sendmsg = fty_proto_encode_metric (
//        NULL,
//        time (NULL),
//        1000,
//        "dev",
//        "UPS33",
//        "1",
//        "c");
//
//    rv = mlm_client_send (m_sender, "subject",  &sendmsg);
    rv = fty::shm::write_metric("UPS33", "dev", "1", "c", wanted_ttl);
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
    rv = mlm_client_send (a_sender, "subject",  &sendmsg);
    assert (rv >= 0);

    // test case 03: add new asset device, wait expiry time and check the alert
    zhash_t *aux = zhash_new ();
    zhash_insert (aux, FTY_PROTO_ASSET_TYPE, (void *) "device");
    zhash_insert (aux, FTY_PROTO_ASSET_SUBTYPE, (void *) "ups");
    zhash_insert (aux, FTY_PROTO_ASSET_STATUS, (void *) "active");
    sendmsg = fty_proto_encode_asset (
        aux,
        "UPS42",
        FTY_PROTO_ASSET_OP_CREATE,
        NULL);
    zhash_destroy (&aux);
    rv = mlm_client_send (a_sender, "UPS42",  &sendmsg);
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
    zhash_insert (aux, FTY_PROTO_ASSET_TYPE, (void *) "device");
    zhash_insert (aux, FTY_PROTO_ASSET_SUBTYPE, (void *) "ups");
    zhash_insert (aux, FTY_PROTO_ASSET_STATUS, (void *) "retired");
    sendmsg = fty_proto_encode_asset (
        aux,
        "UPS42",
        FTY_PROTO_ASSET_OP_UPDATE,
        NULL);
    zhash_destroy (&aux);
    rv = mlm_client_send (a_sender, "UPS42",  &sendmsg);
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
//    mlm_client_destroy (&m_sender);
    fty_shm_delete_test_dir();
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
