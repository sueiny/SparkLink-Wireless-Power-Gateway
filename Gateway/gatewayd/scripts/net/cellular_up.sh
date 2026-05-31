#!/bin/sh
set -u

IFNAME="${1:-ppp0}"
SERIAL_DEVICE="${2:-/dev/ttyS1}"
APN="${3:-cmnet}"

echo "cellular up placeholder: ifname=$IFNAME serial=$SERIAL_DEVICE apn=$APN"
echo "TODO: add L610 PPP/ECM/RNDIS initialization for this board"
exit 0
