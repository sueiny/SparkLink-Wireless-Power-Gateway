#!/bin/bash

# D3QN和Dijkstra顺序测试脚本
# dongle地址为0x10

PORT="/dev/ttyUSB0"
BAUD=115200
NODES="1,2,3,4,5,6,7,8,9"
ROUNDS=2
PAYLOAD="AABBCC"
GATEWAY="10"
ACK_TIMEOUT=2.0
INTERVAL=0.3
BOOT_WAIT=12.0
RSSI_REQUESTS=3

TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_DIR_BASE="app/logs/benchmark_200_${TIMESTAMP}"
LOG_DIR_D3QN="${LOG_DIR_BASE}/d3qn"
LOG_DIR_DIJKSTRA="${LOG_DIR_BASE}/dijkstra"

mkdir -p "$LOG_DIR_D3QN" "$LOG_DIR_DIJKSTRA"

echo "=========================================="
echo "开始200轮顺序测试"
echo "时间: $(date)"
echo "串口: $PORT  网关: $GATEWAY  节点: $NODES"
echo "=========================================="

cd app

echo "[$(date +%H:%M:%S)] 启动Dijkstra测试..."
python -m pc_dijkstra_cli bench \
    --port "$PORT" --baud "$BAUD" --nodes "$NODES" --rounds "$ROUNDS" \
    --payload "$PAYLOAD" --log-dir "../$LOG_DIR_DIJKSTRA" --gateway "$GATEWAY" \
    --boot-wait "$BOOT_WAIT" --rssi-requests "$RSSI_REQUESTS" \
    --ack-timeout "$ACK_TIMEOUT" --interval "$INTERVAL" \
    --route-mode "baseline_dijkstra" --recollect-consecutive-failures 3 \
    2>&1 | tee "../$LOG_DIR_DIJKSTRA/console.log"
DIJKSTRA_EXIT=${PIPESTATUS[0]}
echo "[$(date +%H:%M:%S)] Dijkstra测试完成 (exit=$DIJKSTRA_EXIT)"

echo ""
echo "[$(date +%H:%M:%S)] 启动D3QN测试..."
python -m pc_d3qn_cli bench \
    --port "$PORT" --baud "$BAUD" --nodes "$NODES" --rounds "$ROUNDS" \
    --payload "$PAYLOAD" --log-dir "../$LOG_DIR_D3QN" --gateway "$GATEWAY" \
    --boot-wait "$BOOT_WAIT" --rssi-requests "$RSSI_REQUESTS" \
    --ack-timeout "$ACK_TIMEOUT" --interval "$INTERVAL" \
    --recollect-consecutive-failures 3 \
    --path-loss-degrade-threshold 0.20 --path-p95-degrade-ms 1500 \
    --path-avg-degrade-ms 500 --path-health-window 5 \
    2>&1 | tee "../$LOG_DIR_D3QN/console.log"
D3QN_EXIT=${PIPESTATUS[0]}
echo "[$(date +%H:%M:%S)] D3QN测试完成 (exit=$D3QN_EXIT)"

cd ..
echo "=========================================="
echo "测试完成 $(date)"
echo "Dijkstra: ../$LOG_DIR_DIJKSTRA"
echo "D3QN: ../$LOG_DIR_D3QN"
echo "=========================================="
