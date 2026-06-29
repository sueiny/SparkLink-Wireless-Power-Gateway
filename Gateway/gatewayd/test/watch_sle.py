#!/usr/bin/env python3
"""SLE 数据实时监控 - 串口助手风格"""
import subprocess
import re
import sys
import time

def parse_st_frame(hex_str):
    """解析 ST 帧"""
    parts = hex_str.split()
    if len(parts) < 13:
        return None
    
    magic = parts[0:2]
    version = parts[2]
    frame_type = parts[3]
    src_role = parts[4]
    src_node_id = int(parts[5], 16) | (int(parts[6], 16) << 8)
    dst_node_id = int(parts[7], 16) | (int(parts[8], 16) << 8)
    seq = int(parts[9], 16) | (int(parts[10], 16) << 8)
    payload_len = int(parts[11], 16) | (int(parts[12], 16) << 8)
    
    type_names = {1: "HEARTBEAT", 2: "DATA", 3: "TOPO", 4: "DEPTH"}
    role_names = {1: "ROOT", 2: "RELAY", 3: "LEAF", 4: "GW"}
    
    return {
        "type": type_names.get(int(frame_type, 16), f"0x{frame_type}"),
        "type_hex": frame_type,
        "role": role_names.get(int(src_role, 16), f"0x{src_role}"),
        "src": src_node_id,
        "dst": dst_node_id,
        "seq": seq,
        "payload_len": payload_len,
        "payload_hex": " ".join(parts[13:13+payload_len]) if payload_len > 0 else "",
    }

def print_frame(timestamp, mac, rx_count, hex_str):
    """打印格式化的帧"""
    frame = parse_st_frame(hex_str)
    if not frame:
        return
    
    # 颜色
    CYAN = "\033[36m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    RED = "\033[31m"
    DIM = "\033[2m"
    RESET = "\033[0m"
    
    type_color = {
        "HEARTBEAT": GREEN,
        "DATA": CYAN,
        "TOPO": YELLOW,
        "DEPTH": RED,
    }.get(frame["type"], DIM)
    
    print(f"{DIM}{timestamp}{RESET} | "
          f"#{rx_count:>3} | "
          f"{type_color}{frame['type']:<10}{RESET} | "
          f"src={frame['src']:>3} → dst={frame['dst']:>3} | "
          f"seq={frame['seq']:>3} | "
          f"len={frame['payload_len']:>3} | "
          f"{DIM}{frame['payload_hex'][:60]}{RESET}")

def main():
    print("\033[2J\033[H")  # 清屏
    print("=" * 80)
    print("  SLE 数据实时监控 (串口助手风格)")
    print("  按 Ctrl+C 退出")
    print("=" * 80)
    print()
    
    proc = subprocess.Popen(
        ["adb", "shell", "tail -f /tmp/sle_app.log"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )
    
    try:
        for line in proc.stdout:
            line = line.strip()
            if "[SLE][RX]" not in line:
                # 显示其他重要日志
                if "[SLE]" in line:
                    print(f"\033[2m{line}\033[0m")
                continue
            
            # 提取字段
            m = re.search(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})', line)
            timestamp = m.group(1) if m else ""
            
            m = re.search(r'mac=(\S+)', line)
            mac = m.group(1) if m else ""
            
            m = re.search(r'rx_count=(\d+)', line)
            rx_count = m.group(1) if m else "?"
            
            m = re.search(r'hex=(.+)', line)
            if m:
                hex_str = m.group(1)
                print_frame(timestamp, mac, rx_count, hex_str)
    
    except KeyboardInterrupt:
        print("\n\033[2m已停止监控\033[0m")
        proc.terminate()

if __name__ == "__main__":
    main()
