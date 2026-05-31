#!/bin/sh
set -u

IFNAME="${1:-eth0}"

ip link set "$IFNAME" up
udhcpc -i "$IFNAME" -q -n

