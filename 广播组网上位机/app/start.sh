#!/usr/bin/env bash
# 启动串口守护进程 + Web UI
# 用法: ./start.sh [串口] [波特率]
# 示例: ./start.sh /dev/ttyUSB0 115200

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-115200}"
PYTHON=".venv-d3qn/bin/python"
if [ ! -f "$PYTHON" ]; then PYTHON="python3"; fi

DAEMON_PID=""

cleanup(){
    echo ""
    echo "正在退出..."
    [ -n "$DAEMON_PID" ] && kill "$DAEMON_PID" 2>/dev/null
    exit 0
}
trap cleanup INT TERM

echo "串口: $PORT   波特率: $BAUD"
echo "启动串口守护进程..."
"$PYTHON" -m pc_dijkstra_cli serial-daemon --port "$PORT" --baud "$BAUD" &
DAEMON_PID=$!

# 等待守护进程完成串口初始化（2s 复位 + 缓冲）
sleep 3

echo "启动 Web UI..."
"$PYTHON" -m pc_dijkstra_cli launch

cleanup
