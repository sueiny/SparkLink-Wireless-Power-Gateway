#!/bin/sh
set -u

IFNAME="${1:-}"
SERIAL_DEVICE="${2:-/dev/ttyUSB2}"
APN="${3:-cmnet}"

at_cmd() {
    command="$1"
    timeout_sec="${2:-5}"
    if ! command -v microcom >/dev/null 2>&1; then
        return 1
    fi
    printf '%s\r\n' "$command" | timeout "$timeout_sec" microcom -t 2000 -s 115200 "$SERIAL_DEVICE" 2>/dev/null
}

ensure_host_dialup() {
    if ! at_cmd 'AT' 3 | grep -q 'OK'; then
        echo "ML307 AT check failed: $SERIAL_DEVICE" >&2
        return 1
    fi

    mipcall="$(at_cmd 'AT+MIPCALL?' 5 || true)"
    if ! echo "$mipcall" | grep -q '+MIPCALL: 1,1'; then
        at_cmd 'AT+MIPCALL=1,1' 10 >/dev/null || true
    fi

    at_cmd 'AT+MDIALUPCFG="auto",1' 5 >/dev/null || true

    i=0
    while [ "$i" -lt 3 ]; do
        status="$(at_cmd 'AT+MDIALUP?' 5 || true)"
        if echo "$status" | grep -q '+MDIALUP: 1,1'; then
            return 0
        fi
        sleep 1
        i=$((i + 1))
    done

    echo "ML307 host dialup is not active" >&2
    return 1
}

is_ml307_netdev() {
    name="$1"
    dev="/sys/class/net/$name/device"
    [ -d "$dev" ] || return 1

    driver="$(basename "$(readlink -f "$dev/driver" 2>/dev/null)" 2>/dev/null || true)"
    modalias="$(cat "$dev/modalias" 2>/dev/null || true)"
    product="$(cat "$dev/../product" 2>/dev/null || true)"
    manufacturer="$(cat "$dev/../manufacturer" 2>/dev/null || true)"

    case "$driver" in
        rndis_host|cdc_ether|cdc_ncm) ;;
        *) return 1 ;;
    esac

    echo "$modalias $product $manufacturer" | grep -Eq 'v2ECCp3012|ML307|CMIOT'
}

find_ml307_netdev() {
    for path in /sys/class/net/*; do
        [ -d "$path" ] || continue
        name="$(basename "$path")"
        case "$name" in
            lo|eth1|wlan0|usb0|ppp*|wlan*) continue ;;
        esac
        if is_ml307_netdev "$name"; then
            echo "$name"
            return 0
        fi
    done
    return 1
}

ensure_cellular_ifname() {
    desired="${IFNAME:-cell0}"
    case "$desired" in
        cell0) ;;
        *)
            echo "refusing ML307 cellular ifname other than cell0: $desired" >&2
            return 1
            ;;
    esac

    if [ -d "/sys/class/net/$desired" ]; then
        echo "$desired"
        return 0
    fi

    raw_ifname="$(find_ml307_netdev || true)"
    [ -n "$raw_ifname" ] || return 1
    [ "$raw_ifname" = "$desired" ] && {
        echo "$desired"
        return 0
    }

    ip link set "$raw_ifname" down 2>/dev/null || true
    if ! ip link set "$raw_ifname" name "$desired"; then
        echo "failed to rename ML307 interface $raw_ifname to $desired" >&2
        return 1
    fi
    ip link set "$desired" up 2>/dev/null || true
    echo "$desired"
}

if [ ! -e "$SERIAL_DEVICE" ]; then
    echo "ML307 AT device not found: $SERIAL_DEVICE" >&2
    exit 1
fi

CELL_IFNAME="$(ensure_cellular_ifname || true)"
if [ -z "$CELL_IFNAME" ]; then
    echo "ML307 ECM/RNDIS cell0 interface not found, apn=$APN" >&2
    exit 1
fi

ensure_host_dialup

ip link set "$CELL_IFNAME" up

if ip -4 addr show "$CELL_IFNAME" | grep -q 'inet '; then
    echo "ML307 cellular already has IPv4: $CELL_IFNAME"
    exit 0
fi

if pidof dhcpcd >/dev/null 2>&1; then
    dhcpcd -m 300 "$CELL_IFNAME"
    dhcpcd -n "$CELL_IFNAME"
else
    udhcpc -i "$CELL_IFNAME" -q -n
fi

if ! ip -4 addr show "$CELL_IFNAME" | grep -q 'inet '; then
    echo "ML307 cellular DHCP timeout: $CELL_IFNAME" >&2
    exit 1
fi

echo "ML307 cellular ready: $CELL_IFNAME"
