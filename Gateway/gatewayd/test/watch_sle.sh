#!/usr/bin/env bash
set -euo pipefail

# SLE 串口链路监听助手
#
# 运行位置不限，依赖 adb 可用。此脚本只监听板端现有日志，不启动/停止进程。
# 重点展示：
#   - SLE root 扫描、连接、READY、断开状态
#   - sle_data_app 收到的 ST 05/06/DATA 原始帧
#   - gatewayd 是否接受 05/06、是否发布 MQTT

SLE_APP_LOG="/tmp/sle_data_app.out"
SLE_RX_LOG="/tmp/sle_app.log"
GW_LOG="/userdata/gateway/data/log/gateway.log"
RAW=0
SHOW_SCAN=0

usage() {
    cat <<EOF
usage: $0 [--raw] [--scan]

  --raw   同时显示未匹配的原始日志行
  --scan  实时显示扫描 candidate；默认隐藏，避免刷屏盖住 RX/READY

看连接状态：
  [CONNECTED] / [READY] / [DISCONNECTED] / active_connections

看收包：
  [RX] DATA / [RX] 05 / [RX] 06

板端日志来源：
  ${SLE_APP_LOG}
  ${SLE_RX_LOG}
  ${GW_LOG}
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --raw)
            RAW=1
            shift
            ;;
        --scan)
            SHOW_SCAN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

color() {
    local code="$1"
    shift
    printf '\033[%sm%s\033[0m' "$code" "$*"
}

print_header() {
    echo "============================================================"
    echo "  SLE 串口链路监听助手 (Ctrl+C 退出)"
    echo "============================================================"
    echo "  连接状态日志: ${SLE_APP_LOG}"
    echo "  收包明细日志: ${SLE_RX_LOG}"
    echo "  gatewayd 日志: ${GW_LOG}"
    echo "------------------------------------------------------------"
}

check_board() {
    if ! adb get-state >/dev/null 2>&1; then
        echo "adb device not available" >&2
        exit 1
    fi
}

print_snapshot() {
    echo "$(color 1 '当前快照')"
    adb shell "
        echo '-- processes --'
        ps | grep -E 'gatewayd|sle_data_app' | grep -v grep || true
        if ! ps | grep -E 'sle_data_app' | grep -v grep >/dev/null 2>&1; then
            echo '[SLE][ERROR] sle_data_app is not running; no 05/06/DATA can arrive'
        fi
        echo '-- ipc sockets --'
        cat /proc/net/unix | grep -E 'gateway|sle_data|sle_cmd' || true
        echo '-- latest connection state --'
        grep -E '\\[SLE\\]\\[ADDR\\]|\\[SLE\\]\\[CONNECTED\\]|\\[SLE\\]\\[READY\\]|\\[SLE\\]\\[DISCONNECTED\\]|active_connections' ${SLE_APP_LOG} 2>/dev/null | tail -20 || true
        echo '-- latest SLE errors/warnings --'
        grep -E '\\[SLE\\]\\[(ERROR|WARN)\\]|manager init failed|init failed|accept failed|Bad file descriptor' ${SLE_APP_LOG} ${SLE_RX_LOG} 2>/dev/null | tail -20 || true
        echo '-- latest scan candidates --'
        grep -E 'candidate tree' ${SLE_APP_LOG} 2>/dev/null | tail -8 || true
        echo '-- latest rx --'
        grep -E '\\[SLE\\]\\[RX\\]' ${SLE_RX_LOG} ${SLE_APP_LOG} 2>/dev/null | tail -8 || true
        echo '-- rx/topology counters --'
        printf 'sle_rx=%s gateway_topology=%s\n' \
            \"\$(grep -h -c '\\[SLE\\]\\[RX\\]' ${SLE_RX_LOG} ${SLE_APP_LOG} 2>/dev/null | awk '{s+=\$1} END{print s+0}')\" \
            \"\$(grep -E -c 'ST 0x05|ST 0x06|topology|external' ${GW_LOG} 2>/dev/null || true)\"
        echo '-- latest gateway --'
        grep -E 'ST 0x05|ST 0x06|SLE-IPC|SLE-DATA|MQTT.*publish|cloud_connected|sle-daemon|no sle-daemon' ${GW_LOG} 2>/dev/null | tail -12 || true
    " | sed 's/\r$//'
    echo "------------------------------------------------------------"
}

hex_to_text() {
    local hex="$1"
    local out=""
    local byte dec
    for byte in $hex; do
        [[ "$byte" =~ ^[0-9A-Fa-f]{2}$ ]] || continue
        dec=$((16#$byte))
        case "$dec" in
            10) out+="↵" ;;
            13) out+="⏎" ;;
            9)  out+="→" ;;
            *)
                if (( dec >= 32 && dec <= 126 )); then
                    printf -v ch '%b' "\\x$(printf '%02x' "$dec")"
                    out+="$ch"
                else
                    out+="·"
                fi
                ;;
        esac
    done
    printf '%s' "$out"
}

hex_byte_at() {
    local hex="$1"
    local pos="$2"
    awk -v n="$pos" '{print $n}' <<<"$hex"
}

le16_hex() {
    local lo="$1"
    local hi="$2"
    if [[ ! "$lo" =~ ^[0-9A-Fa-f]{2}$ || ! "$hi" =~ ^[0-9A-Fa-f]{2}$ ]]; then
        printf '0'
        return
    fi
    printf '%u' $((16#$lo + (16#$hi << 8)))
}

frame_name() {
    case "$1" in
        02|02) printf 'DATA' ;;
        05|05) printf 'DTU_NETWORK_TOPOLOGY' ;;
        06|06) printf 'EXTERNAL_DEVICE_MAP' ;;
        *)     printf 'UNKNOWN' ;;
    esac
}

extract_hex() {
    local line="$1"
    local after
    after="${line#*hex=}"
    if [[ "$after" == "$line" ]]; then
        printf ''
        return
    fi
    after="${after%% ascii=*}"
    echo "$after" | tr -s ' ' | sed 's/^ *//; s/ *$//'
}

print_rx_line() {
    local source="$1"
    local line="$2"
    local ts len rx server conn mac hex type name role src_lo src_hi dst_lo dst_hi seq_lo seq_hi plen_lo plen_hi src dst seq plen payload payload_text
    ts="$(date +%H:%M:%S)"
    len="$(sed -n 's/.*len=\([0-9][0-9]*\).*/\1/p' <<<"$line")"
    rx="$(sed -n 's/.*rx_count=\([0-9][0-9]*\).*/\1/p' <<<"$line")"
    server="$(sed -n 's/.*server_index=\([-0-9][0-9]*\).*/\1/p' <<<"$line")"
    conn="$(sed -n 's/.*conn_id=\([0-9][0-9]*\).*/\1/p' <<<"$line")"
    mac="$(sed -n 's/.*mac=\([0-9a-fA-F:][0-9a-fA-F:]*\).*/\1/p' <<<"$line")"
    hex="$(extract_hex "$line")"

    type="$(hex_byte_at "$hex" 4)"
    role="$(hex_byte_at "$hex" 5)"
    src_lo="$(hex_byte_at "$hex" 6)"
    src_hi="$(hex_byte_at "$hex" 7)"
    dst_lo="$(hex_byte_at "$hex" 8)"
    dst_hi="$(hex_byte_at "$hex" 9)"
    seq_lo="$(hex_byte_at "$hex" 10)"
    seq_hi="$(hex_byte_at "$hex" 11)"
    plen_lo="$(hex_byte_at "$hex" 12)"
    plen_hi="$(hex_byte_at "$hex" 13)"
    src="$(le16_hex "$src_lo" "$src_hi")"
    dst="$(le16_hex "$dst_lo" "$dst_hi")"
    seq="$(le16_hex "$seq_lo" "$seq_hi")"
    plen="$(le16_hex "$plen_lo" "$plen_hi")"
    name="$(frame_name "$type")"
    payload="$(cut -d' ' -f14- <<<"$hex")"
    payload_text="$(hex_to_text "$payload")"

    case "$type" in
        02)
            echo -e "$(color 2 "$ts") $(color 36 "[RX][$source]") #${rx:-?} ${name} len=${len:-?} root/src=${src} dst=${dst} seq=${seq} payload=${plen} mac=${mac:-?} server=${server:-?} conn=${conn:-?}"
            echo -e "    $(color 2 "modbus: ${payload}")"
            ;;
        05)
            echo -e "$(color 2 "$ts") $(color 35 "[RX][$source]") #${rx:-?} ${name} len=${len:-?} root=${src} seq=${seq} payload=${plen} mac=${mac:-?}"
            echo -e "    $(color 33 "topo: ${payload_text}")"
            ;;
        06)
            echo -e "$(color 2 "$ts") $(color 33 "[RX][$source]") #${rx:-?} ${name} len=${len:-?} root=${src} seq=${seq} payload=${plen} mac=${mac:-?}"
            if [[ -n "$payload" ]]; then
                echo -e "    $(color 33 "map: ${payload_text}")"
            else
                echo -e "    $(color 2 "map: <empty>")"
            fi
            ;;
        *)
            echo -e "$(color 2 "$ts") $(color 31 "[RX][$source]") #${rx:-?} type=0x${type:-??} len=${len:-?} src=${src} dst=${dst} seq=${seq} payload=${plen} mac=${mac:-?}"
            echo -e "    $(color 2 "hex: ${hex}")"
            ;;
    esac
}

print_state_line() {
    local line="$1"
    local ts
    ts="$(date +%H:%M:%S)"

    if [[ "$line" == *"[SLE][ADDR]"* ]]; then
        echo -e "$(color 2 "$ts") $(color 36 "[ADDR]") ${line}"
    elif [[ "$line" == *"[SLE][CONNECTED]"* ]]; then
        echo -e "$(color 2 "$ts") $(color 32 "[CONNECTED]") ${line}"
    elif [[ "$line" == *"[SLE][READY]"* ]]; then
        echo -e "$(color 2 "$ts") $(color 32 "[READY]") ${line}"
    elif [[ "$line" == *"[SLE][DISCONNECTED]"* ]]; then
        echo -e "$(color 2 "$ts") $(color 31 "[DISCONNECTED]") ${line}"
    elif [[ "$line" == *"[SLE][ERROR]"* || "$line" == *"manager init failed"* ]]; then
        echo -e "$(color 2 "$ts") $(color 31 "[SLE][ERROR]") ${line}"
    elif [[ "$line" == *"[SLE][WARN]"* || "$line" == *"accept failed"* ]]; then
        echo -e "$(color 2 "$ts") $(color 33 "[SLE][WARN]") ${line}"
    elif [[ "$line" == *"active_connections="* ]]; then
        echo -e "$(color 2 "$ts") $(color 32 "[ACTIVE]") ${line}"
    elif [[ "$line" == *"candidate tree="* ]]; then
        if [[ "$SHOW_SCAN" -eq 1 ]]; then
            echo -e "$(color 2 "$ts") $(color 34 "[SCAN]") ${line}"
        fi
    elif [[ "$line" == *"[CMD][ST-"* ]]; then
        echo -e "$(color 2 "$ts") $(color 36 "[DOWNLINK]") ${line}"
    elif [[ "$RAW" -eq 1 ]]; then
        echo -e "$(color 2 "$ts") $(color 2 "$line")"
    fi
}

print_gateway_line() {
    local line="$1"
    local ts
    ts="$(date +%H:%M:%S)"

    if [[ "$line" == *"ST 0x05 accepted"* ]]; then
        echo -e "$(color 2 "$ts") $(color 35 "[GW][05 OK]") ${line}"
    elif [[ "$line" == *"ST 0x06 accepted"* ]]; then
        echo -e "$(color 2 "$ts") $(color 33 "[GW][06 OK]") ${line}"
    elif [[ "$line" == *"ST 0x05 parse failed"* || "$line" == *"ST 0x06 parse failed"* || "$line" == *"drop DATA"* ]]; then
        echo -e "$(color 2 "$ts") $(color 31 "[GW][DROP]") ${line}"
    elif [[ "$line" == *"SLE-IPC"* || "$line" == *"sle-daemon"* ]]; then
        echo -e "$(color 2 "$ts") $(color 33 "[GW][SLE-IPC]") ${line}"
    elif [[ "$line" == *"telemetry batch"* || "$line" == *"publish success"* || "$line" == *"cloud_connected"* ]]; then
        echo -e "$(color 2 "$ts") $(color 32 "[GW]") ${line}"
    elif [[ "$RAW" -eq 1 ]]; then
        echo -e "$(color 2 "$ts") $(color 2 "$line")"
    fi
}

handle_line() {
    local line="$1"
    local source="${line%%|*}"
    local msg="${line#*|}"
    msg="${msg%$'\r'}"

    case "$source" in
        APP|RX)
            if [[ "$msg" == *"[SLE][RX]"* ]]; then
                print_rx_line "$source" "$msg"
            else
                print_state_line "$msg"
            fi
            ;;
        GW)
            print_gateway_line "$msg"
            ;;
        *)
            [[ "$RAW" -eq 1 ]] && echo "$msg"
            ;;
    esac
}

print_header
check_board
print_snapshot

echo "$(color 1 '开始实时监听...')"
echo "提示：看到 [CONNECTED] 后只是链路连上；看到 [READY] 后才是数据通道可用；看到 [RX] 才是收到具体 ST 数据。"
echo "------------------------------------------------------------"

adb shell "touch ${SLE_APP_LOG} ${SLE_RX_LOG} ${GW_LOG}; \
    tail -n 0 -F ${SLE_APP_LOG} 2>/dev/null | while IFS= read -r line; do echo \"APP|\$line\"; done & \
    tail -n 0 -F ${SLE_RX_LOG} 2>/dev/null | while IFS= read -r line; do echo \"RX|\$line\"; done & \
    tail -n 0 -F ${GW_LOG} 2>/dev/null | while IFS= read -r line; do echo \"GW|\$line\"; done & \
    wait" | while IFS= read -r line; do
    handle_line "$line"
done
