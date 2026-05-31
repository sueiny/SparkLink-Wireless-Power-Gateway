# gatewayd

RK3506 first-stage edge gateway demo.

This version reads a JSON config, generates mock telemetry for meters,
environment sensors, and relays, maps it to ThingsKit-style JSON, and writes
the output to stdout and `/userdata/gateway/data/log/gateway.log`.

## Build

```sh
make -C app/Gateway/gatewayd
```

If `libmosquitto` is not in the default Buildroot staging path, pass the
mosquitto sysroot/staging prefix:

```sh
make -C app/Gateway/gatewayd MOSQUITTO_ROOT=/path/to/sysroot
```

The ARM executable is generated at:

```text
app/Gateway/gatewayd/build-cmake/gatewayd
```

## Deploy With ADB

```sh
make -C app/Gateway/gatewayd push
adb shell /userdata/gateway/bin/gatewayd --config /userdata/gateway/config/gateway_config.json
```

MQTT channel smoke test:

```sh
adb shell /userdata/gateway/bin/gatewayd --config /userdata/gateway/config/gateway_config.json --mqtt-test
```

## Run Locally On Board

```sh
cd /userdata/gateway/bin
./gatewayd --config /userdata/gateway/config/gateway_config.json
```

Use `Ctrl+C` to stop.
