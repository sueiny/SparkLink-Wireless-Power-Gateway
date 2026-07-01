#!/bin/sh
set -u

ADB_ROOT="${ADB_ROOT:-/userdata/gateway}"
CONFIG="$ADB_ROOT/config/gateway_config.json"
BACKUP="$ADB_ROOT/config/gateway_config.json.bak_network_test"
LOG="$ADB_ROOT/data/log/gateway.log"
GATEWAYD="$ADB_ROOT/bin/gatewayd"
CLOUD_TIMEOUT_SEC="${CLOUD_TIMEOUT_SEC:-60}"
SELECT_TIMEOUT_SEC="${SELECT_TIMEOUT_SEC:-120}"
FAILED_MODES=""

fail_mode() {
    mode="$1"
    message="$2"
    echo "[FAIL][$mode] $message" >&2
    FAILED_MODES="${FAILED_MODES}${FAILED_MODES:+ }$mode"
}

adb_shell() {
    adb shell "$@"
}

restore_config() {
    echo "[INFO] restoring gateway config"
    adb_shell "if [ -f '$BACKUP' ]; then cp '$BACKUP' '$CONFIG'; fi" >/dev/null 2>&1 || true
}

require_adb() {
    if ! command -v adb >/dev/null 2>&1; then
        echo "adb not found" >&2
        exit 1
    fi
    adb get-state >/dev/null 2>&1 || {
        echo "adb device is not ready" >&2
        exit 1
    }
}

backup_config() {
    adb_shell "test -f '$CONFIG'" >/dev/null || {
        echo "board config not found: $CONFIG" >&2
        exit 1
    }
    adb_shell "cp '$CONFIG' '$BACKUP'" >/dev/null || {
        echo "failed to backup config" >&2
        exit 1
    }
}

set_network_mode() {
    mode="$1"
    adb_shell "awk -v new_mode='$mode' '
        BEGIN { in_network = 0; changed = 0 }
        /\"network\"[[:space:]]*:/ { in_network = 1 }
        in_network && /\"mode\"[[:space:]]*:/ && !changed {
            sub(/\"mode\"[[:space:]]*:[[:space:]]*\"[^\"]*\"/, \"\\\"mode\\\": \\\"\" new_mode \"\\\"\")
            changed = 1
        }
        in_network && /^[[:space:]]*}/ { in_network = 0 }
        { print }
    ' '$CONFIG' > /tmp/gateway_config.network_test && cp /tmp/gateway_config.network_test '$CONFIG'" >/dev/null
}

restart_gatewayd() {
    adb_shell "killall -9 gatewayd 2>/dev/null; rm -f '$LOG' /tmp/gatewayd.log; mkdir -p '$ADB_ROOT/data/log'; nohup '$GATEWAYD' --config '$CONFIG' >/tmp/gatewayd.log 2>&1 &" >/dev/null
}

wait_for_log_pattern() {
    pattern="$1"
    timeout_sec="$2"
    i=0
    while [ "$i" -lt "$timeout_sec" ]; do
        if adb_shell "grep -E '$pattern' '$LOG' >/dev/null 2>&1"; then
            return 0
        fi
        sleep 1
        i=$((i + 1))
    done
    return 1
}

selected_ifname() {
    mode="$1"
    value="$(adb_shell "grep -E 'selected $mode |keep selected $mode ' '$LOG' 2>/dev/null | tail -1 | sed -n 's/.*$mode \\([^ ,]*\\).*/\\1/p'" | tr -d '\r')"
    if [ -n "$value" ]; then
        echo "$value"
        return 0
    fi

    adb_shell "grep -E '\"network_type\":\"$mode\"' '$LOG' 2>/dev/null | tail -1 | sed -n 's/.*\"network_ifname\":\"\\([^\"]*\\)\".*/\\1/p'" | tr -d '\r'
}

mode_observed() {
    mode="$1"
    if adb_shell "grep -E 'selected $mode |keep selected $mode |\"network_type\":\"$mode\"' '$LOG' >/dev/null 2>&1"; then
        return 0
    fi
    return 1
}

wait_for_mode_observed() {
    mode="$1"
    timeout_sec="$2"
    i=0
    while [ "$i" -lt "$timeout_sec" ]; do
        if mode_observed "$mode"; then
            return 0
        fi
        sleep 1
        i=$((i + 1))
    done
    return 1
}

check_common_mode() {
    mode="$1"
    expected_ifname="$2"

    echo ""
    echo "===== testing network.mode=$mode ====="
    set_network_mode "$mode" || {
        fail_mode "$mode" "failed to update config"
        return
    }
    restart_gatewayd

    if ! wait_for_mode_observed "$mode" "$SELECT_TIMEOUT_SEC"; then
        fail_mode "$mode" "gatewayd did not select $mode"
    fi

    if ! wait_for_log_pattern 'cloud_connected":1|cloud_connected=1' "$CLOUD_TIMEOUT_SEC"; then
        fail_mode "$mode" "cloud_connected did not become 1"
    fi

    actual_ifname="$(selected_ifname "$mode")"
    if [ -z "$actual_ifname" ]; then
        actual_ifname="$expected_ifname"
    fi

    if [ -n "$expected_ifname" ] && [ "$actual_ifname" != "$expected_ifname" ]; then
        fail_mode "$mode" "expected ifname $expected_ifname but selected $actual_ifname"
    fi

    if ! adb_shell "ip route show default | grep -q ' dev $actual_ifname '"; then
        fail_mode "$mode" "default route is not on $actual_ifname"
    fi

    if ! adb_shell "grep -q '^nameserver ' /etc/resolv.conf"; then
        fail_mode "$mode" "resolv.conf has no nameserver"
    fi

    adb_shell "echo '-- ip addr $actual_ifname --'; ip -4 addr show '$actual_ifname' 2>/dev/null; echo '-- routes --'; ip route show default; echo '-- dns --'; cat /etc/resolv.conf; echo '-- gateway log --'; grep -E 'NET|MQTT|cloud_connected|selected|keep selected|dns realigned|cannot reach cloud' '$LOG' 2>/dev/null | tail -80"
}

find_board_cellular_ifname() {
    adb_shell "[ -d /sys/class/net/cell0 ] && echo cell0" | tr -d '\r'
}

check_cellular_mode() {
    echo ""
    echo "===== testing network.mode=cellular ====="

    if ! adb_shell "test -e /dev/ttyUSB2"; then
        fail_mode "cellular" "/dev/ttyUSB2 not found"
    fi
    if ! adb_shell "printf 'AT\r\n' >/dev/ttyUSB2"; then
        fail_mode "cellular" "AT write failed on /dev/ttyUSB2"
    fi

    cell_ifname="$(find_board_cellular_ifname || true)"
    if [ -z "$cell_ifname" ]; then
        fail_mode "cellular" "ML307 ECM/RNDIS cell0 interface not found"
        cell_ifname="cell0"
    fi
    if [ "$cell_ifname" != "cell0" ]; then
        fail_mode "cellular" "expected cellular ifname cell0 but found $cell_ifname"
    fi

    check_common_mode "cellular" "$cell_ifname"

    selected="$(selected_ifname cellular)"
    if [ -n "$selected" ]; then
        if [ "$selected" != "cell0" ]; then
            fail_mode "cellular" "gatewayd selected invalid cellular ifname $selected"
        fi
        if ! adb_shell "ip -4 addr show '$selected' | grep -q 'inet '"; then
            fail_mode "cellular" "$selected has no IPv4"
        fi
    fi
}

main() {
    require_adb
    backup_config
    trap restore_config EXIT INT TERM

    check_cellular_mode
    check_common_mode "ethernet" "eth1"
    check_common_mode "wifi" "wlan0"

    restore_config
    trap - EXIT INT TERM

    if [ -n "$FAILED_MODES" ]; then
        echo ""
        echo "[FAIL] failed modes: $FAILED_MODES" >&2
        exit 1
    fi

    echo ""
    echo "[PASS] fixed network modes passed: cellular ethernet wifi"
}

main "$@"
