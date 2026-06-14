"""串口守护进程 — 保持串口长期开启，所有 CLI 工具通过 Unix socket 连接。

用法:
  python -m pc_dijkstra_cli serial-daemon --port /dev/ttyUSB0
"""
from __future__ import annotations

import os
import socket
import threading
import time


def get_socket_path(port: str) -> str:
    return f"/tmp/pc_serial_{os.path.basename(port)}.sock"


def run_daemon(port: str, baud: int = 115200) -> None:
    try:
        import serial
    except ImportError:
        raise RuntimeError("pyserial is required: pip install pyserial")

    socket_path = get_socket_path(port)

    print(f"正在连接串口 {port}...")
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        timeout=0.05,
        write_timeout=1,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    )
    ser.dtr = False
    ser.rts = False
    time.sleep(2.0)
    ser.read(ser.in_waiting or 8192)
    print(f"串口 {port} 已连接")

    clients: list[socket.socket] = []
    clients_lock = threading.Lock()
    serial_write_lock = threading.Lock()

    if os.path.exists(socket_path):
        os.unlink(socket_path)

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(socket_path)
    server.listen(8)
    print(f"守护进程已启动，监听 {socket_path}")
    print("按 Ctrl+C 停止")

    def serial_reader() -> None:
        while True:
            try:
                data = ser.read(4096)
            except Exception:
                break
            if not data:
                continue
            with clients_lock:
                dead = []
                for c in clients:
                    try:
                        c.sendall(data)
                    except Exception:
                        dead.append(c)
                for c in dead:
                    clients.remove(c)
        # 串口出错：关闭所有已连接客户端，再关闭 server socket
        # 这样后续 benchmark 连接时立即得到 ConnectionRefused，而不是 BrokenPipeError
        print("\n⚠️  串口读取失败，关闭守护进程", flush=True)
        with clients_lock:
            for c in clients:
                try:
                    c.close()
                except Exception:
                    pass
            clients.clear()
        try:
            server.close()
        except Exception:
            pass
        try:
            os.unlink(socket_path)
        except Exception:
            pass

    def handle_client(conn: socket.socket) -> None:
        with clients_lock:
            clients.append(conn)
        try:
            while True:
                try:
                    data = conn.recv(4096)
                except Exception:
                    break
                if not data:
                    break
                with serial_write_lock:
                    try:
                        ser.write(data)
                        ser.flush()
                    except Exception:
                        break
        finally:
            with clients_lock:
                if conn in clients:
                    clients.remove(conn)
            try:
                conn.close()
            except Exception:
                pass

    threading.Thread(target=serial_reader, daemon=True).start()

    try:
        while True:
            try:
                conn, _ = server.accept()
            except OSError:
                print("串口已断开，守护进程退出", flush=True)
                break
            threading.Thread(target=handle_client, args=(conn,), daemon=True).start()
    except KeyboardInterrupt:
        print("\n守护进程已停止")
    finally:
        server.close()
        if os.path.exists(socket_path):
            os.unlink(socket_path)
        ser.close()
