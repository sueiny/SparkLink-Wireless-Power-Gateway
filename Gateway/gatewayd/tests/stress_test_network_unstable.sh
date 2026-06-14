#!/bin/bash
# 压力测试 4: 网络不稳定模拟测试
# 模拟网络抖动、延迟、丢包

TEST_DURATION=300  # 测试时长（秒）

echo "=== 压力测试 4: 网络不稳定模拟测试 ==="
echo "测试时长: ${TEST_DURATION}秒"
echo ""

# 监控函数
monitor_gateway() {
    local log_file="/tmp/stress_test_unstable_$(date +%Y%m%d_%H%M%S).log"
    echo "监控日志: $log_file"

    while true; do
        timestamp=$(date '+%Y-%m-%d %H:%M:%S')

        # 检查进程状态
        if ! adb shell "ps aux | grep -q '[g]atewayd'"; then
            echo "[$timestamp] ❌ gatewayd 进程已退出!" | tee -a "$log_file"
            break
        fi

        # 检查网络延迟
        latency=$(adb shell "ping -c 1 -W 1 8.8.8.8 2>/dev/null | grep 'time=' | awk -F'time=' '{print \$2}' | awk '{print \$1}' || echo 'timeout'")

        # 检查 MQTT 连接状态
        cloud_status=$(adb shell "cat /tmp/gateway_status.json 2>/dev/null | grep -o '\"cloud_connected\":[0-9]' | cut -d: -f2 || echo 'unknown')

        # 检查线程状态
        thread_count=$(adb shell "ls /proc/\$(pidof gatewayd)/task 2>/dev/null | wc -l || echo 0")

        # 检查内存使用
        mem_usage=$(adb shell "cat /proc/\$(pidof gatewayd)/status 2>/dev/null | grep VmRSS | awk '{print \$2}' || echo 0")

        echo "[$timestamp] latency=${latency}ms cloud=$cloud_status threads=$thread_count mem=${mem_usage}kB" | tee -a "$log_file"

        sleep 2
    done
}

# 模拟网络抖动
simulate_jitter() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local jitter_ms=$((100 + RANDOM % 500))

    echo "[$timestamp] 🔄 模拟网络抖动: ${jitter_ms}ms 延迟..."

    # 添加延迟
    adb shell "tc qdisc add dev wlan0 root netem delay ${jitter_ms}ms 2>/dev/null || true"

    sleep 2

    # 移除延迟
    adb shell "tc qdisc del dev wlan0 root 2>/dev/null || true"
    echo "[$timestamp] ✅ 移除网络抖动"
}

# 模拟丢包
simulate_packet_loss() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local loss_percent=$((10 + RANDOM % 40))

    echo "[$timestamp] 🔄 模拟丢包: ${loss_percent}%..."

    # 添加丢包
    adb shell "tc qdisc add dev wlan0 root netem loss ${loss_percent}% 2>/dev/null || true"

    sleep 3

    # 移除丢包
    adb shell "tc qdisc del dev wlan0 root 2>/dev/null || true"
    echo "[$timestamp] ✅ 移除丢包模拟"
}

# 模拟网络中断
simulate_outage() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local duration=$((2 + RANDOM % 5))

    echo "[$timestamp] 🔄 模拟网络中断: ${duration}秒..."

    # 阻止所有出站流量
    adb shell "iptables -A OUTPUT -j DROP 2>/dev/null"

    sleep $duration

    # 恢复出站流量
    adb shell "iptables -D OUTPUT -j DROP 2>/dev/null"
    echo "[$timestamp] ✅ 恢复网络连接"
}

# 模拟 DNS 故障
simulate_dns_failure() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    echo "[$timestamp] 🔄 模拟 DNS 故障..."

    # 备份 resolv.conf
    adb shell "cp /etc/resolv.conf /tmp/resolv.conf.bak 2>/dev/null"

    # 清空 DNS 配置
    adb shell "echo '# DNS failure simulation' > /etc/resolv.conf"

    sleep 5

    # 恢复 DNS 配置
    adb shell "cp /tmp/resolv.conf.bak /etc/resolv.conf 2>/dev/null"
    echo "[$timestamp] ✅ 恢复 DNS 配置"
}

# 主测试逻辑
echo "开始监控..."
monitor_gateway &
MONITOR_PID=$!

echo "开始网络不稳定测试..."
START_TIME=$(date +%s)

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))

    if [ $ELAPSED -ge $TEST_DURATION ]; then
        echo "测试完成，已运行 ${ELAPSED} 秒"
        break
    fi

    # 随机选择测试场景
    SCENE=$((RANDOM % 5))
    case $SCENE in
        0) simulate_jitter ;;
        1) simulate_packet_loss ;;
        2) simulate_outage ;;
        3) simulate_dns_failure ;;
        4) simulate_jitter; sleep 1; simulate_packet_loss ;;
    esac

    # 随机等待时间
    WAIT_TIME=$((3 + RANDOM % 10))
    sleep $WAIT_TIME
done

# 停止监控
kill $MONITOR_PID 2>/dev/null
wait $MONITOR_PID 2>/dev/null

# 清理 iptables 规则
adb shell "iptables -F OUTPUT 2>/dev/null"
adb shell "tc qdisc del dev wlan0 root 2>/dev/null"

echo ""
echo "=== 测试完成 ==="
echo "检查日志文件: /tmp/stress_test_unstable_*.log"