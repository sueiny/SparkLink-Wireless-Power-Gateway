#!/bin/bash
# Gateway 构建、部署、测试驱动脚本
# 用法: driver.sh [build-sle|build-gw|push|test|full]

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
    adb shell "killall gatewayd 2>/dev/null; killall sle_data_app 2>/dev/null; sleep 1"

    # 清理 socket
    adb shell "rm -f /var/run/gateway/sle_data.sock; mkdir -p /var/run/gateway"

    # 启动 gatewayd
    echo "启动 gatewayd..."
    adb shell "nohup $ADB_ROOT/bin/gatewayd --config $ADB_ROOT/config/gateway_config.json > /tmp/gatewayd.log 2>&1 &"

    # 等待 MQTT 连接
    echo "等待 MQTT 连接..."
    for i in $(seq 1 30); do
        if adb shell "grep -q 'cloud_connected.*1' $ADB_ROOT/data/log/gateway.log 2>/dev/null"; then
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
    full)       full ;;
    *)
        echo "用法: $0 [build-sle|build-gw|build-ipc|gen-test|push|test|full]"
        echo ""
        echo "  build-sle   编译 sle_data_app"
        echo "  build-gw    编译 gatewayd"
        echo "  build-ipc   编译 ipc_send 工具"
        echo "  gen-test    生成测试数据"
        echo "  push        推送到板端"
        echo "  test        板端测试"
        echo "  full        全流程"
        ;;
esac
