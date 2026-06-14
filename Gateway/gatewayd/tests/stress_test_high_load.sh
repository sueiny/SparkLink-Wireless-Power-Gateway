#!/bin/bash
# 压力测试 3: 高负载数据发送测试
# 模拟大量设备同时发送数据

TEST_DURATION=300  # 测试时长（秒）
DEVICE_COUNT=50    # 模拟设备数量
SEND_INTERVAL=1    # 发送间隔（秒）

echo "=== 压力测试 3: 高负载数据发送测试 ==="
echo "测试时长: ${TEST_DURATION}秒"
echo "模拟设备: ${DEVICE_COUNT}个"
echo "发送间隔: ${SEND_INTERVAL}秒"
echo ""

# 监控函数
monitor_gateway() {
    local log_file="/tmp/stress_test_load_$(date +%Y%m%d_%H%M%S).log"
    echo "监控日志: $log_file"

    while true; do
        timestamp=$(date '+%Y-%m-%d %H:%M:%S')

        # 检查进程状态
        if ! adb shell "ps aux | grep -q '[g]atewayd'"; then
            echo "[$timestamp] ❌ gatewayd 进程已退出!" | tee -a "$log_file"
            break
        fi

        # 检查队列状态
        queue_status=$(adb shell "cat /tmp/gateway_status.json 2>/dev/null || echo '{}'")

        # 检查线程状态
        thread_count=$(adb shell "ls /proc/\$(pidof gatewayd)/task 2>/dev/null | wc -l || echo 0")

        # 检查内存使用
        mem_usage=$(adb shell "cat /proc/\$(pidof gatewayd)/status 2>/dev/null | grep VmRSS | awk '{print \$2}' || echo 0")

        # 检查 CPU 使用
        cpu_usage=$(adb shell "top -bn1 | grep gatewayd | awk '{print \$7}' || echo 0")

        echo "[$timestamp] threads=$thread_count mem=${mem_usage}kB cpu=${cpu_usage}%" | tee -a "$log_file"
        echo "[$timestamp] queue: $queue_status" | tee -a "$log_file"

        sleep 2
    done
}

# 生成模拟数据
generate_telemetry() {
    local device_id=$1
    local timestamp=$(date +%s)

    cat <<EOF
{
    "device_id": "stress_device_${device_id}",
    "timestamp": ${timestamp},
    "data": {
        "temperature": $((20 + RANDOM % 30)),
        "humidity": $((40 + RANDOM % 40)),
        "pressure": $((1000 + RANDOM % 50)),
        "voltage": $((3000 + RANDOM % 1000)),
        "current": $((100 + RANDOM % 500))
    }
}
EOF
}

# 发送数据到设备
send_data() {
    local device_id=$1
    local data=$(generate_telemetry $device_id)

    # 通过 SLE 发送数据（如果可用）
    # 这里简化为直接写入文件模拟
    echo "$data" > /tmp/stress_device_${device_id}.json

    # 或者通过 MQTT 发送（如果可用）
    # mosquitto_pub -h localhost -t "v1/devices/me/telemetry" -m "$data"
}

# 主测试逻辑
echo "开始监控..."
monitor_gateway &
MONITOR_PID=$!

echo "开始高负载测试..."
START_TIME=$(date +%s)

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))

    if [ $ELAPSED -ge $TEST_DURATION ]; then
        echo "测试完成，已运行 ${ELAPSED} 秒"
        break
    fi

    # 模拟多个设备同时发送数据
    for i in $(seq 1 $DEVICE_COUNT); do
        send_data $i &
    done

    # 等待所有发送完成
    wait

    sleep $SEND_INTERVAL
done

# 停止监控
kill $MONITOR_PID 2>/dev/null
wait $MONITOR_PID 2>/dev/null

# 清理临时文件
rm -f /tmp/stress_device_*.json

echo ""
echo "=== 测试完成 ==="
echo "检查日志文件: /tmp/stress_test_load_*.log"