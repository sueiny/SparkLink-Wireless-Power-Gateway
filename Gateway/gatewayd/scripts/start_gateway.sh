#!/bin/sh
# Gateway 自动启动脚本
# 用法: /userdata/gateway/bin/start_gateway.sh

GATEWAY_DIR="/userdata/gateway"
LOG_DIR="$GATEWAY_DIR/data/log"
mkdir -p "$LOG_DIR" /var/run/gateway

# 停止旧进程
killall gatewayd 2>/dev/null
killall sle_data_app 2>/dev/null
sleep 1

# 清理旧 socket
rm -f /var/run/gateway/sle_data.sock

# 启动 gatewayd（会自动设置路由优先级）
nohup $GATEWAY_DIR/bin/gatewayd --config $GATEWAY_DIR/config/gateway_config.json \
    > /tmp/gatewayd.log 2>&1 &

# 等待 gatewayd 初始化
sleep 3

# 启动 sle_data_app
nohup $GATEWAY_DIR/bin/sle_data_app \
    > /tmp/sle_data_app.log 2>&1 &

echo "Gateway started at $(date)" >> "$LOG_DIR/startup.log"
