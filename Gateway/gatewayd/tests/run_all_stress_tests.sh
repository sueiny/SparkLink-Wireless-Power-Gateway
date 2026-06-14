#!/bin/bash
# 主压力测试脚本
# 运行所有压力测试并收集结果

echo "=========================================="
echo "Gatewayd 压力测试套件"
echo "=========================================="
echo ""

# 检查设备连接
echo "检查设备连接..."
if ! adb devices | grep -q "device$"; then
    echo "❌ 未检测到设备，请确保 ADB 连接正常"
    exit 1
fi
echo "✅ 设备已连接"

# 检查 gatewayd 进程
echo "检查 gatewayd 进程..."
if ! adb shell "ps aux | grep -q '[g]atewayd'"; then
    echo "❌ gatewayd 未运行，请先启动 gatewayd"
    exit 1
fi
echo "✅ gatewayd 正在运行"

# 获取测试开始时间
START_TIME=$(date '+%Y%m%d_%H%M%S')
RESULTS_DIR="/tmp/stress_test_results_${START_TIME}"
mkdir -p "$RESULTS_DIR"

echo ""
echo "测试结果将保存到: $RESULTS_DIR"
echo ""

# 运行测试 1: 网络切换压力测试
echo "=========================================="
echo "测试 1: 网络切换压力测试"
echo "=========================================="
bash stress_test_network_switch.sh 2>&1 | tee "$RESULTS_DIR/test1_network_switch.log"
echo ""

# 等待系统稳定
echo "等待系统稳定..."
sleep 30

# 运行测试 2: MQTT 断连重连测试
echo "=========================================="
echo "测试 2: MQTT 断连重连测试"
echo "=========================================="
bash stress_test_mqtt_reconnect.sh 2>&1 | tee "$RESULTS_DIR/test2_mqtt_reconnect.log"
echo ""

# 等待系统稳定
echo "等待系统稳定..."
sleep 30

# 运行测试 3: 高负载数据测试
echo "=========================================="
echo "测试 3: 高负载数据测试"
echo "=========================================="
bash stress_test_high_load.sh 2>&1 | tee "$RESULTS_DIR/test3_high_load.log"
echo ""

# 等待系统稳定
echo "等待系统稳定..."
sleep 30

# 运行测试 4: 网络不稳定测试
echo "=========================================="
echo "测试 4: 网络不稳定测试"
echo "=========================================="
bash stress_test_network_unstable.sh 2>&1 | tee "$RESULTS_DIR/test4_network_unstable.log"
echo ""

# 生成测试报告
echo "=========================================="
echo "生成测试报告"
echo "=========================================="

REPORT_FILE="$RESULTS_DIR/test_report.txt"
cat > "$REPORT_FILE" <<EOF
Gatewayd 压力测试报告
测试时间: $(date)
设备信息: $(adb shell "uname -a")
Gatewayd 版本: $(adb shell "/userdata/gateway/bin/gatewayd --version 2>&1 || echo 'unknown'")

测试结果:
=========

1. 网络切换压力测试
   日志文件: test1_network_switch.log
   状态: $(grep -c "❌" "$RESULTS_DIR/test1_network_switch.log" > /dev/null && echo "失败" || echo "通过")

2. MQTT 断连重连测试
   日志文件: test2_mqtt_reconnect.log
   状态: $(grep -c "❌" "$RESULTS_DIR/test2_mqtt_reconnect.log" > /dev/null && echo "失败" || echo "通过")

3. 高负载数据测试
   日志文件: test3_high_load.log
   状态: $(grep -c "❌" "$RESULTS_DIR/test3_high_load.log" > /dev/null && echo "失败" || echo "通过")

4. 网络不稳定测试
   日志文件: test4_network_unstable.log
   状态: $(grep -c "❌" "$RESULTS_DIR/test4_network_unstable.log" > /dev/null && echo "失败" || echo "通过")

总结:
=====
$(grep -c "❌" "$RESULTS_DIR"/*.log > /dev/null && echo "❌ 发现问题，请检查日志" || echo "✅ 所有测试通过")
EOF

echo "测试报告已生成: $REPORT_FILE"
echo ""
cat "$REPORT_FILE"

echo ""
echo "=========================================="
echo "压力测试完成"
echo "=========================================="
echo "结果目录: $RESULTS_DIR"
echo "查看详细日志: ls -lh $RESULTS_DIR/"
echo ""