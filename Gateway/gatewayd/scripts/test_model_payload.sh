#!/bin/sh

# 物模型优化阶段的独立测试脚本。
# 这个脚本只验证 ThingsKit topic 和 JSON 字段格式，不依赖 gatewayd 进程。
# 使用网关 MQTT 凭证登录，然后按“网关 + 网关子设备”协议发布测试数据。

set -eu

HOST="${HOST:-thingskit.aiotcomm.com.cn}"
PORT="${PORT:-11883}"
CLIENT_ID="${CLIENT_ID:-46dc3ebf25bf4cdb9cd01deb6092b7ef}"
USERNAME="${USERNAME:-123}"
PASSWORD="${PASSWORD:-123}"

DEVICE_ATTR_TOPIC="v1/devices/me/attributes"
GATEWAY_TELEMETRY_TOPIC="v1/gateway/telemetry"

publish() {
    topic="$1"
    payload="$2"

    echo "[MODEL_TEST] publish topic=${topic}"
    mosquitto_pub \
        -h "${HOST}" \
        -p "${PORT}" \
        -i "${CLIENT_ID}" \
        -u "${USERNAME}" \
        -P "${PASSWORD}" \
        -q 1 \
        -t "${topic}" \
        -m "${payload}"
}

gateway_attributes='{"network_type":"wifi","network_ifname":"wlan0","cloud_connected":true,"device_count":7,"cache_count":0,"gateway_version":"model-test","network_status":{"network_type":"wifi","network_ifname":"wlan0","cloud_connected":true}}'

# 子设备遥测格式：外层 key 是 ThingsKit 中已经绑定到网关的子设备编号。
# 数值枚举统一用数字：例如 meter_role 0=总表/1=支表，relay_status 0=拉闸/1=合闸。
# 本脚本同时保留原有扁平字段，并新增 STRUCT 字段，用来验证平台是否接受嵌套对象。
subdevice_telemetry='{"METER_MAIN_001":[{"voltage":221.1,"current":15.8,"active_power":3420.5,"power_factor":0.96,"frequency":50.01,"energy":10524.66,"relay_status":1,"meter_role":0,"parent_meter_id":"","branch_power_sum":3300.2,"power_loss":120.3,"loss_rate":3.65,"meter_loss":{"meter_loss_branch_power_sum":3300.2,"meter_loss_power_loss":120.3,"meter_loss_rate":3.65},"online":true}],"METER_BRANCH_001":[{"voltage":220.4,"current":3.2,"active_power":681.4,"power_factor":0.95,"frequency":50.0,"energy":3122.52,"relay_status":1,"meter_role":1,"parent_meter_id":"METER_MAIN_001","branch_power_sum":0,"power_loss":0,"loss_rate":0,"meter_loss":{"meter_loss_branch_power_sum":0,"meter_loss_power_loss":0,"meter_loss_rate":0},"online":true}],"ENV_001":[{"temperature":29.6,"humidity":63.2,"online":true}],"RELAY_001":[{"relay_state":1,"control_mode":0,"online":true}],"DTU_NODE_001":[{"role":0,"mac":"AA:BB:CC:00:00:01","name":"DTU_NODE_001","online":true,"uptime":3600,"parent_mac":"","child_count":1,"child_macs":"AA:BB:CC:00:00:02","topology":{"parent_mac":"","child_count":1,"child_macs":"AA:BB:CC:00:00:02"},"modbus_count":6,"collect_cycle":5000,"collect_config":{"modbus_count":6,"collect_cycle":5000}}]}'

publish "${DEVICE_ATTR_TOPIC}" "${gateway_attributes}"
publish "${GATEWAY_TELEMETRY_TOPIC}" "${subdevice_telemetry}"

echo "[MODEL_TEST] done"
