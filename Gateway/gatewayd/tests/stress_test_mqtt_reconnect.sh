#!/bin/bash
# 压力测试 2: MQTT 断连重连风暴测试
# 频繁断开 MQTT 连接，观察重连机制

TEST_DURATION=180  # 测试时长（秒）
DISCONNECT_INTERVAL=3  # 断开间隔（秒）

echo "=== 压力测试 2: MQTT 断连重连风暴测试 ==="
echo "测试时长: ${TEST_DURATION}秒"
echo "断开间隔: ${DISCONNECT_INTERVAL}秒"
echo ""

# 监控函数
monitor_gateway() {
    local log_file="/tmp/stress_test_mqtt_$(date +%Y%m%d_%H%M%S).log"
    echo "监控日志: $log_file"

    while true; do
        timestamp=$(date '+%Y-%m-%d %H:%M:%S')

        # 检查进程状态
        if ! adb shell "ps aux | grep -q '[g]atewayd'"; then
            echo "[$timestamp] ❌ gatewayd 进程已退出!" | tee -a "$log_file"
            break
        fi

        # 检查 MQTT 连接状态
        cloud_status=$(adb shell "cat /tmp/gateway_status.json 2>/dev/null | grep -o '\"cloud_connected\":[0-9]' | cut -d: -f2 || echo 'unknown')

        # 检查网络连接
        mqtt_conn=$(adb shell "netstat -tn 2>/dev/null | grep ':1883' | grep ESTABLISHED | wc -l || echo 0")

        # 检查线程状态
        thread_count=$(adb shell "ls /proc/\$(pidof gatewayd)/task 2>/dev/null | wc -l || echo 0")

        # 检查内存使用
        mem_usage=$(adb shell "cat /proc/\$(pidof gatewayd)/status 2>/dev/null | grep VmRSS | awk '{print \$2}' || echo 0")

        echo "[$timestamp] cloud=$cloud_status mqtt_conn=$mqtt_conn threads=$thread_count mem=${mem_usage}kB" | tee -a "$log_file"

        sleep 2
    done
}

# 断开 MQTT 连接
disconnect_mqtt() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] 🔌 断开 MQTT 连接..."

    # 方法 1: 阻止 MQTT 端口
    adb shell "iptables -A OUTPUT -p tcp --dport 1883 -j DROP 2>/dev/null"

    sleep 1

    # 方法 2: 恢复 MQTT 端口
    adb shell "iptables -D OUTPUT -p tcp --dport 1883 -j DROP 2>/dev/null"
    echo "[$timestamp] ✅ 恢复 MQTT 连接"
}

# 主测试逻辑
echo "开始监控..."
monitor_gateway &
MONITOR_PID=$!

echo "开始 MQTT 断连重连测试..."
START_TIME=$(date +%s)

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))

    if [ $ELAPSED -ge $TEST_DURATION ]; then
        echo "测试完成，已运行 ${ELAPSED} 秒"
        break
    fi

    # 断开并重连 MQTT
    disconnect_mqtt

    sleep $DISCONNECT_INTERVAL
done

# 停止监控
kill $MONITOR_PID 2>/dev/null
wait $MONITOR_PID 2>/dev/null

echo ""
echo "=== 测试完成 ==="
echo "检查日志文件: /tmp/stress_test_mqtt_*.log"