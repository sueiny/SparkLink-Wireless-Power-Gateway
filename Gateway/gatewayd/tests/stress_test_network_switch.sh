#!/bin/bash
# 压力测试 1: 网络切换压力测试
# 快速切换 wifi/eth 接口，观察 gatewayd 稳定性

BOARD_IP="192.168.1.100"  # 设备 IP，需要根据实际情况修改
TEST_DURATION=300  # 测试时长（秒）
SWITCH_INTERVAL=5  # 切换间隔（秒）

echo "=== 压力测试 1: 网络切换压力测试 ==="
echo "测试时长: ${TEST_DURATION}秒"
echo "切换间隔: ${SWITCH_INTERVAL}秒"
echo ""

# 监控函数
monitor_gateway() {
    local log_file="/tmp/stress_test_1_$(date +%Y%m%d_%H%M%S).log"
    echo "监控日志: $log_file"

    while true; do
        timestamp=$(date '+%Y-%m-%d %H:%M:%S')

        # 检查进程状态
        if ! adb shell "ps aux | grep -q '[g]atewayd'"; then
            echo "[$timestamp] ❌ gatewayd 进程已退出!" | tee -a "$log_file"
            break
        fi

        # 检查网络状态
        wifi_status=$(adb shell "cat /sys/class/net/wlan0/operstate 2>/dev/null || echo 'down'")
        eth_status=$(adb shell "cat /sys/class/net/eth0/operstate 2>/dev/null || echo 'down'")

        # 检查 MQTT 连接状态
        cloud_status=$(adb shell "cat /tmp/gateway_status.json 2>/dev/null | grep -o '\"cloud_connected\":[0-9]' | cut -d: -f2 || echo 'unknown'")

        # 检查线程状态
        thread_count=$(adb shell "ls /proc/\$(pidof gatewayd)/task 2>/dev/null | wc -l || echo 0")

        # 检查内存使用
        mem_usage=$(adb shell "cat /proc/\$(pidof gatewayd)/status 2>/dev/null | grep VmRSS | awk '{print \$2}' || echo 0")

        echo "[$timestamp] wifi=$wifi_status eth=$eth_status cloud=$cloud_status threads=$thread_count mem=${mem_usage}kB" | tee -a "$log_file"

        sleep 2
    done
}

# 网络切换函数
switch_network() {
    local action=$1
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    case $action in
        "disable_wifi")
            echo "[$timestamp] 🔄 禁用 WiFi..."
            adb shell "ifconfig wlan0 down"
            ;;
        "enable_wifi")
            echo "[$timestamp] 🔄 启用 WiFi..."
            adb shell "ifconfig wlan0 up"
            ;;
        "disable_eth")
            echo "[$timestamp] 🔄 禁用以太网..."
            adb shell "ifconfig eth0 down"
            ;;
        "enable_eth")
            echo "[$timestamp] 🔄 启用以太网..."
            adb shell "ifconfig eth0 up"
            ;;
        "restart_network")
            echo "[$timestamp] 🔄 重启网络服务..."
            adb shell "/etc/init.d/S40network restart"
            ;;
    esac
}

# 主测试逻辑
echo "开始监控..."
monitor_gateway &
MONITOR_PID=$!

echo "开始网络切换测试..."
START_TIME=$(date +%s)

while true; do
    CURRENT_TIME=$(date +%s)
    ELAPSED=$((CURRENT_TIME - START_TIME))

    if [ $ELAPSED -ge $TEST_DURATION ]; then
        echo "测试完成，已运行 ${ELAPSED} 秒"
        break
    fi

    # 随机选择切换动作
    ACTION=$((RANDOM % 6))
    case $ACTION in
        0) switch_network "disable_wifi" ;;
        1) switch_network "enable_wifi" ;;
        2) switch_network "disable_eth" ;;
        3) switch_network "enable_eth" ;;
        4) switch_network "disable_wifi"; sleep 2; switch_network "enable_wifi" ;;
        5) switch_network "disable_eth"; sleep 2; switch_network "enable_eth" ;;
    esac

    sleep $SWITCH_INTERVAL
done

# 停止监控
kill $MONITOR_PID 2>/dev/null
wait $MONITOR_PID 2>/dev/null

echo ""
echo "=== 测试完成 ==="
echo "检查日志文件: /tmp/stress_test_1_*.log"