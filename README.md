# fty-outage

Agent fty-outage produces pure alerts on \_ALERTS\_SYS when no data are coming from the device.

## How to build

To build fty-outage project run:

```bash
./autogen.sh
./configure
make
make check # to run self-test
```

## How to run

To run fty-outage project:

* from within the source tree, run:

```bash
./src/fty-outage
```

For the other options available, refer to the manual page of fty-outage

* from an installed base, using systemd, run:

```bash
systemctl start fty-outage
```

### Configuration file

Configuration file - fty-outage.cfg - is currently ignored.

Agent reads environment variable BIOS\_LOG\_LEVEL which controls verbosity level.

State file for fty-outage is stored in /var/lib/fty/fty-outage.zpl.

## Architecture

### Overview

fty-outage is composed of 1 actor and 2 timera.

* fty-outage-server: main actor

First timer is implemented via checking zclock and saves the state of the agent each SAVE\_INTERVAL\_MS milliseconds (default value 45 minutes).

Second timer is implemented via zpoller timeout and publishes outage alerts for dead devices every TIMEOUT\_MS milliseconds (default value 30 seconds) unless such an alert is already active.

## Protocols

### Published metrics

Agent doesn't publish any metrics.

### Published alerts

Agent publishes alerts on \_ALERTS\_SYS stream.

### Mailbox requests

Agent fty-outage-server doesn't support any mailbox requests.

### Stream subscriptions

Agent is subscribed to streams METRICS, METRICS\_UNAVAILABLE, METRICS\_SENSOR and ASSETS.

If it gets METRICS\_UNAVAILABLE message, it resolves all the stored alerts for specified device.

If it gets METRICS or METRICS\_SENSOR message from a device, it resolves all the stored alerts for specified device and marks the device as active.

If it gets ASSETS message, it updates the asset cache. If the message is for operation DELETE or RETIRE, it resolves all the alerts for specified device.
