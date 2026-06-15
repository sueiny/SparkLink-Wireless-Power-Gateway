#!/bin/bash
# Gateway 构建、部署、测试驱动脚本
# 用法: driver.sh [build-sle|build-gw|push|test|test-real-listen|watch-real|full]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GATEWAY_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
PROJECT_ROOT="$(cd "$GATEWAY_DIR/../.." && pwd)"

# 交叉编译工具链
CROSS="$PROJECT_ROOT/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-"
SYSROOT="$PROJECT_ROOT/buildroot/output/rockchip_rk3506_emmc/host/arm-buildroot-linux-gnueabihf/sysroot"

# 板端路径
ADB_ROOT="/userdata/gateway"

# 清理 PATH（buildroot 不接受含空格的 PATH）
clean_path() {
    export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$HOME/.local/bin"
    export http_proxy=""
    export https_proxy=""
    export HTTP_PROXY=""
    export HTTPS_PROXY=""
}

# ── 编译 sle_data_app ──
build_sle() {
    echo "=== 编译 sle_data_app ==="
    cd "$GATEWAY_DIR/sle_data_app"
    make clean 2>/dev/null || true
    make
    echo "✅ sle_data_app 编译完成: $(ls -la sle_data_app | awk '{print $5, $9}')"
}

# ── 编译 gatewayd ──
build_gw() {
    echo "=== 编译 gatewayd ==="
    clean_path
    cd "$GATEWAY_DIR/gatewayd"

    # 检查 sysroot 是否有 mosquitto 和 sqlite
    if [ ! -f "$SYSROOT/usr/include/mosquitto.h" ]; then
        echo "❌ 缺少 mosquitto，尝试构建 buildroot 包..."
        cd "$PROJECT_ROOT/buildroot"
        make sqlite mosquitto
        cd "$GATEWAY_DIR/gatewayd"
    fi

    rm -rf build-cmake
    cmake --build build-cmake --parallel 2>/dev/null || {
        # 需要重新 configure
        cmake -S . -B build-cmake \
            -DCMAKE_TOOLCHAIN_FILE=cmake/rk3506-armhf-toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DGATEWAYD_MOSQUITTO_ROOT="$SYSROOT" \
            -DGATEWAYD_SQLITE_ROOT="$SYSROOT"
        cmake --build build-cmake --parallel
    }
    echo "✅ gatewayd 编译完成: $(ls -la build-cmake/gatewayd | awk '{print $5, $9}')"
}

# ── 推送到板端 ──
push() {
    echo "=== 推送到板端 ==="

    # sle_data_app
    adb push "$GATEWAY_DIR/sle_data_app/sle_data_app" "$ADB_ROOT/bin/"

    # gatewayd
    adb push "$GATEWAY_DIR/gatewayd/build-cmake/gatewayd" "$ADB_ROOT/bin/"

    # 配置文件
    adb push "$GATEWAY_DIR/gatewayd/config/gateway_config.json" "$ADB_ROOT/config/"

    # 物模型
    adb shell "rm -rf $ADB_ROOT/things_model"
    adb push "$GATEWAY_DIR/gatewayd/things_model" "$ADB_ROOT/things_model"

    # 测试数据和工具
    adb shell "mkdir -p $ADB_ROOT/test"
    adb push "$GATEWAY_DIR/gatewayd/test/test_payload.bin" "$ADB_ROOT/test/"
    adb push "$GATEWAY_DIR/gatewayd/test/ipc_send" "$ADB_ROOT/test/"

    # 权限
    adb shell "chmod +x $ADB_ROOT/bin/sle_data_app $ADB_ROOT/bin/gatewayd $ADB_ROOT/test/ipc_send"

    # 确保 SLE 启用
    adb shell "sed -i 's/\"enable\": false/\"enable\": true/' $ADB_ROOT/config/gateway_config.json"

    echo "✅ 推送完成"
}

# ── 编译 ipc_send 工具 ──
build_ipc_send() {
    echo "=== 编译 ipc_send ==="
    "${CROSS}gcc" -Os -s -o "$GATEWAY_DIR/gatewayd/test/ipc_send" \
        "$GATEWAY_DIR/gatewayd/test/ipc_send.c"
    echo "✅ ipc_send 编译完成"
}

# ── 生成测试数据 ──
gen_test() {
    echo "=== 生成测试数据 ==="
    cd "$GATEWAY_DIR/gatewayd/test"
    python3 gen_test_frames.py
    python3 -c "
import json, struct
with open('test_frames.json') as f:
    frames = json.load(f)
out = bytearray()
for fr in frames:
    raw = bytes(fr['raw_data'][:fr['raw_len']])
    out += struct.pack('<H', len(raw))
    out += raw
with open('test_payload.bin', 'wb') as f:
    f.write(out)
print(f'Generated {len(out)} bytes')
"
    build_ipc_send
    echo "✅ 测试数据生成完成"
}

# ── 板端测试 ──
test() {
    echo "=== 板端测试 ==="

    # 停止旧进程
    adb shell "killall -9 gatewayd 2>/dev/null; killall -9 sle_data_app 2>/dev/null; sleep 1"

    # 清理 socket
    adb shell "rm -f /var/run/gateway/sle_data.sock; mkdir -p /var/run/gateway"
    adb shell "rm -f $ADB_ROOT/data/log/gateway.log /tmp/gatewayd.log"

    # 启动 gatewayd
    echo "启动 gatewayd..."
    adb shell "nohup $ADB_ROOT/bin/gatewayd --config $ADB_ROOT/config/gateway_config.json > /tmp/gatewayd.log 2>&1 &"

    echo "等待 SLE IPC socket..."
    socket_ready=0
    for i in $(seq 1 30); do
        if adb shell "cat /proc/net/unix | grep -q '@var/run/gateway/sle_data.sock'"; then
            echo "✅ SLE IPC socket 已监听"
            socket_ready=1
            break
        fi
        sleep 1
    done
    if [ "$socket_ready" -ne 1 ]; then
        echo "❌ SLE IPC socket 未监听"
        adb shell "tail -80 /tmp/gatewayd.log 2>/dev/null; tail -80 $ADB_ROOT/data/log/gateway.log 2>/dev/null"
        return 1
    fi

    # 等待 MQTT 连接
    echo "等待 MQTT 连接..."
    for i in $(seq 1 30); do
        if adb shell "grep -q '\"cloud_connected\":1' $ADB_ROOT/data/log/gateway.log 2>/dev/null"; then
            echo "✅ MQTT 已连接"
            break
        fi
        sleep 2
    done

    # 发送测试数据
    echo "发送测试数据..."
    adb shell "$ADB_ROOT/test/ipc_send /var/run/gateway/sle_data.sock $ADB_ROOT/test/test_payload.bin"

    # 等待数据处理
    sleep 10

    # 检查结果
    echo ""
    echo "=== 测试结果 ==="
    adb shell "grep -E 'SLE-IPC.*batch|MQTT.*publish.*success.*telemetry|cloud_connected' $ADB_ROOT/data/log/gateway.log | tail -5"

    echo ""
    echo "=== 缓存的遥测数据 ==="
    adb shell "$ADB_ROOT/bin/sqlite3 $ADB_ROOT/data/gateway.db 'SELECT payload FROM telemetry_cache ORDER BY id DESC LIMIT 1'" 2>/dev/null | python3 -c "
import json, sys
try:
    data = json.loads(sys.stdin.read())
    for dev_id, entries in sorted(data.items()):
        v = entries[0]['values']
        fields = ', '.join(f'{k}={v[k]}' for k in sorted(v.keys()) if k not in ('mac','src_node_id','online'))
        print(f'  {dev_id}: {fields}')
except:
    print('  (无数据)')
" 2>/dev/null
}

# ── 真实 SLE 监听准备 ──
test_real_listen() {
    echo "=== 真实 SLE 监听准备 ==="

    adb shell "killall -9 gatewayd 2>/dev/null; killall -9 sle_data_app 2>/dev/null; sleep 1"
    adb shell "rm -f /var/run/gateway/sle_data.sock; mkdir -p /var/run/gateway"
    adb shell "rm -f $ADB_ROOT/data/log/gateway.log /tmp/gatewayd.log /tmp/sle_data_app.out /tmp/sle_app.log /tmp/sle_stack_raw.log"

    echo "启动 gatewayd(SLE 模式)..."
    adb shell "nohup $ADB_ROOT/bin/gatewayd --config $ADB_ROOT/config/gateway_config.json > /tmp/gatewayd.log 2>&1 &"

    echo "等待 SLE IPC socket..."
    socket_ready=0
    for i in $(seq 1 30); do
        if adb shell "cat /proc/net/unix | grep -q '@var/run/gateway/sle_data.sock'"; then
            echo "✅ SLE IPC socket 已监听"
            socket_ready=1
            break
        fi
        sleep 1
    done
    if [ "$socket_ready" -ne 1 ]; then
        echo "❌ SLE IPC socket 未监听"
        adb shell "tail -80 /tmp/gatewayd.log 2>/dev/null; tail -80 $ADB_ROOT/data/log/gateway.log 2>/dev/null"
        return 1
    fi

    echo "等待 MQTT 连接..."
    mqtt_ready=0
    for i in $(seq 1 30); do
        if adb shell "grep -q '\"cloud_connected\":1' $ADB_ROOT/data/log/gateway.log 2>/dev/null"; then
            echo "✅ MQTT 已连接"
            mqtt_ready=1
            break
        fi
        sleep 2
    done
    if [ "$mqtt_ready" -ne 1 ]; then
        echo "⚠️ MQTT 未在等待窗口内确认连接，仍启动真实 SLE 监听"
    fi

    echo "启动 sle_data_app(real)..."
    adb shell "nohup $ADB_ROOT/bin/sle_data_app --mode real > /tmp/sle_data_app.out 2>&1 &"
    sleep 2

    echo ""
    echo "=== 进程状态 ==="
    adb shell "ps | grep -E 'gatewayd|sle_data_app' | grep -v grep"

    echo ""
    echo "=== sle_data_app 启动日志 ==="
    adb shell "tail -40 /tmp/sle_data_app.out 2>/dev/null"

    echo ""
    echo "真实监听已启动。Windows 串口侧发送示例（完整拓扑压测：31 个 DTU 心跳 + 11 个外接设备 DATA/轮）："
    echo "cd C:\\Temp\\GatewayTest && py -3 .\\dtu_root_run_sender.py COM19 COM23 COM36 --scenario topology-all --duration 60 --interval 5 --line-delay 0.02 --warmup-sec 5 --warmup-interval 0.2 --warmup-text 12123213 --post-warmup-delay 8 --hold-open 10"
    echo "或在仓库 test 目录双击/运行 run_dtu_root_real.bat。"
    echo ""
    echo "持续监听可运行："
    echo "bash $GATEWAY_DIR/.claude/skills/run-gateway/driver.sh watch-real"
}

# ── 真实 SLE 日志监听 ──
watch_real() {
    echo "=== 监听真实 SLE/Gateway 日志，Ctrl+C 结束 ==="
    adb shell "tail -f /tmp/sle_data_app.out /tmp/sle_app.log $ADB_ROOT/data/log/gateway.log"
}

# ── 全流程 ──
full() {
    build_sle
    build_gw
    gen_test
    push
    test
}

# ── 主入口 ──
case "${1:-help}" in
    build-sle)  build_sle ;;
    build-gw)   build_gw ;;
    build-ipc)  build_ipc_send ;;
    gen-test)   gen_test ;;
    push)       push ;;
    test)       test ;;
    test-real-listen) test_real_listen ;;
    watch-real) watch_real ;;
    full)       full ;;
    *)
        echo "用法: $0 [build-sle|build-gw|build-ipc|gen-test|push|test|test-real-listen|watch-real|full]"
        echo ""
        echo "  build-sle   编译 sle_data_app"
        echo "  build-gw    编译 gatewayd"
        echo "  build-ipc   编译 ipc_send 工具"
        echo "  gen-test    生成测试数据"
        echo "  push        推送到板端"
        echo "  test        板端测试"
        echo "  test-real-listen  启动 gatewayd + sle_data_app --mode real，等待串口侧真实数据"
        echo "  watch-real  持续监听真实 SLE/Gateway 日志"
        echo "  full        全流程"
        ;;
esac
