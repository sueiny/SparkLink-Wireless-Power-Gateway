#!/bin/sh
# Gateway 自动启动脚本
# 用法: /userdata/gateway/bin/start_gateway.sh

GATEWAY_DIR="/userdata/gateway"
LOG_DIR="$GATEWAY_DIR/data/log"
mkdir -p "$LOG_DIR" /var/run/gateway

if [ -x /etc/init.d/S95gateway ]; then
    /etc/init.d/S95gateway restart
    exit $?
fi

# 停止旧进程
killall gatewayd 2>/dev/null
killall sle_data_app 2>/dev/null
sleep 1

# 清理旧 socket
rm -f /var/run/gateway/sle_data.sock

# 启动 gatewayd（会自动设置路由优先级）
nohup $GATEWAY_DIR/bin/gatewayd --config $GATEWAY_DIR/config/gateway_config.json \
    > /tmp/gatewayd.log 2>&1 &

# 等待 gatewayd 监听抽象 SLE IPC socket
for i in $(seq 1 30); do
    if grep -q '@var/run/gateway/sle_data.sock' /proc/net/unix 2>/dev/null; then
        break
    fi
    sleep 1
done

# 启动真实 SLE 数据链路
nohup $GATEWAY_DIR/bin/sle_data_app --mode real \
    > /tmp/sle_data_app.log 2>&1 &

echo "Gateway started at $(date)" >> "$LOG_DIR/startup.log"
