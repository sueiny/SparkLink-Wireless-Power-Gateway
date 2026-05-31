#!/usr/bin/env python3
"""
SLE网关测试 - 主机端发送脚本 (Windows版)
往串口发送测试数据，用于网关端接收测试

用法:
    python send_test.py COM22
    python send_test.py COM22 COM26 COM28  # 同时测试多个串口
"""

import serial
import struct
import time
import sys
import threading
from datetime import datetime


class SerialSender:
    """串口发送器"""
    
    def __init__(self, port: str, baud_rate: int = 115200):
        self.port = port
        self.baud_rate = baud_rate
        self.ser = None
        self.sent_count = 0
        self.running = False
        self.thread = None
    
    def open(self):
        """打开串口"""
        self.ser = serial.Serial(self.port, self.baud_rate, timeout=0.1)
        print(f"[{self.port}] 串口已打开")
    
    def close(self):
        """关闭串口"""
        if self.ser:
            self.ser.close()
            print(f"[{self.port}] 串口已关闭")
    
    def send_loop(self, duration: int, packet_size: int, interval: float):
        """发送循环"""
        self.running = True
        self.sent_count = 0
        
        # 构造测试数据包
        # 格式: [SEQ_H][SEQ_L][DATA...]
        data_size = packet_size - 2  # 减去序列号的2字节
        base_data = bytes(range(256))[:data_size]
        
        start_time = time.time()
        seq = 0
        
        print(f"[{self.port}] 开始发送: 时长={duration}s, 包大小={packet_size}B, 间隔={interval}s")
        
        while self.running and (time.time() - start_time) < duration:
            # 构造数据包
            packet = struct.pack('>H', seq & 0xFFFF) + base_data
            
            # 发送
            try:
                self.ser.write(packet)
                self.sent_count += 1
                seq += 1
            except Exception as e:
                print(f"[{self.port}] 发送错误: {e}")
                break
            
            # 等待
            time.sleep(interval)
        
        elapsed = time.time() - start_time
        print(f"[{self.port}] 发送完成: {self.sent_count}包, 耗时={elapsed:.2f}s, "
              f"速率={self.sent_count/elapsed:.1f}pkt/s")
    
    def stop(self):
        """停止发送"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=2)


def run_single_sender(port: str, duration: int, packet_size: int, interval: float):
    """运行单个发送器"""
    sender = SerialSender(port)
    try:
        sender.open()
        sender.send_loop(duration, packet_size, interval)
    except Exception as e:
        print(f"[{port}] 错误: {e}")
    finally:
        sender.close()
    return sender.sent_count


def main():
    # 默认配置
    default_ports = ['COM22', 'COM26', 'COM28']
    duration = 10        # 测试时长(秒)
    packet_size = 64     # 数据包大小(字节)
    interval = 0.01      # 发送间隔(秒) = 10ms
    baud_rate = 115200   # 波特率
    
    # 解析命令行参数
    if len(sys.argv) > 1:
        ports = sys.argv[1:]
    else:
        ports = default_ports
    
    # 打印配置
    print("=" * 60)
    print("SLE网关性能测试 - 发送端 (Windows)")
    print("=" * 60)
    print(f"串口列表: {', '.join(ports)}")
    print(f"波特率:   {baud_rate}")
    print(f"测试时长: {duration}秒")
    print(f"数据包大小: {packet_size}字节")
    print(f"发送间隔: {interval}秒 ({interval*1000:.0f}ms)")
    print(f"预期速率: {1/interval:.0f} 包/秒/通道")
    print("=" * 60)
    
    # 检查pyserial是否安装
    try:
        import serial
    except ImportError:
        print("\n错误: 未安装pyserial库")
        print("请运行: pip install pyserial")
        sys.exit(1)
    
    # 启动发送线程
    threads = []
    results = {}
    
    start_time = time.time()
    
    for port in ports:
        def thread_func(p=port):
            count = run_single_sender(p, duration, packet_size, interval)
            results[p] = count
        
        t = threading.Thread(target=thread_func, daemon=True)
        threads.append(t)
        t.start()
    
    # 等待所有线程完成
    for t in threads:
        t.join()
    
    total_time = time.time() - start_time
    
    # 打印结果
    print("\n" + "=" * 60)
    print("发送统计")
    print("=" * 60)
    
    total_sent = 0
    for port, count in results.items():
        print(f"{port}: {count}包")
        total_sent += count
    
    print("-" * 60)
    print(f"总计: {total_sent}包")
    print(f"总耗时: {total_time:.2f}秒")
    print(f"平均速率: {total_sent/total_time:.1f} 包/秒")
    print("=" * 60)
    
    print("\n提示: 现在可以在网关端运行 analyze_log.py 分析日志")
    print("      adb pull /tmp/sle_app.log .")
    print("      python analyze_log.py sle_app.log")


if __name__ == '__main__':
    main()
