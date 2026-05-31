#!/bin/sh
set -u

IFNAME="${1:-wlan0}"
SSID="${2:-}"
PASS="${3:-}"
CONF="/tmp/wpa_gateway_${IFNAME}.conf"

if [ -z "$SSID" ]; then
    echo "wifi ssid is empty" >&2
    exit 1
fi

cat > "$CONF" <<EOF
ctrl_interface=/var/run/wpa_supplicant
update_config=1
country=CN

network={
    ssid="$SSID"
    scan_ssid=1
    psk="$PASS"
    key_mgmt=WPA-PSK
}
EOF

ip link set "$IFNAME" up
killall wpa_supplicant 2>/dev/null || true
killall udhcpc 2>/dev/null || true
rm -f "/var/run/wpa_supplicant/$IFNAME"
wpa_supplicant -B -i "$IFNAME" -c "$CONF"

for _ in 1 2 3 4 5 6 7 8 9 10; do
    if iw dev "$IFNAME" link 2>/dev/null | grep -q '^Connected'; then
        break
    fi
    sleep 1
done

if ! iw dev "$IFNAME" link 2>/dev/null | grep -q '^Connected'; then
    echo "wifi connect timeout: $IFNAME $SSID" >&2
    exit 1
fi

USED_DHCPCD=0
if pidof dhcpcd >/dev/null 2>&1; then
    USED_DHCPCD=1
    dhcpcd -m 100 "$IFNAME" || true
    dhcpcd -n "$IFNAME" || true
else
    udhcpc -i "$IFNAME" -q -n
fi

for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    if ip addr show "$IFNAME" | grep -q 'inet '; then
        break
    fi
    sleep 1
done

if ! ip addr show "$IFNAME" | grep -q 'inet '; then
    echo "wifi dhcp timeout: $IFNAME $SSID" >&2
    exit 1
fi

GATEWAY="$(ip route | awk -v dev="$IFNAME" '$1 == "default" && $0 ~ ("dev " dev) { print $3; exit }')"
if [ "$USED_DHCPCD" -eq 0 ] && [ -n "$GATEWAY" ]; then
    while ip route del default 2>/dev/null; do
        true
    done
    ip route add default via "$GATEWAY" dev "$IFNAME" metric 100
    ip route flush cache 2>/dev/null || true
fi
