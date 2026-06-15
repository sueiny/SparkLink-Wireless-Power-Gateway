#!/usr/bin/env python3
"""
SLE网关测试 - 日志分析脚本
解析sle_data_app的日志，计算丢包率和吞吐量

用法:
    py -3 analyze_log.py sle_app.log
    adb shell "cat /tmp/sle_app.log" | python3 analyze_log.py -
"""

import re
import sys
from collections import defaultdict
from datetime import datetime


def parse_log(log_source):
    """
    解析日志文件
    
    日志格式:
    [SLE][RX] server_index=0 conn_id=0 mac=12:00:00:00:00:a4 len=5 rx_count=18 hex=31 32 33 0d 0a ascii="123.."
    """
    # 正则匹配 - 注意 [SLE][RX] 是两个独立的方括号
    pattern = r'\[SLE\]\[RX\]\s+server_index=(\d+)\s+conn_id=(\d+)\s+mac=([0-9a-f:]+)\s+len=(\d+)\s+rx_count=(\d+)'
    
    # 按mac地址分组
    devices = defaultdict(lambda: {
        'server_index': -1,
        'conn_id': -1,
        'rx_counts': [],
        'data_lens': [],
        'count': 0
    })
    
    line_count = 0
    match_count = 0
    
    # 读取输入
    if log_source == '-':
        lines = list(sys.stdin)
    else:
        with open(log_source, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    
    for line in lines:
        line_count += 1
        match = re.search(pattern, line)
        if match:
            match_count += 1
            server_index = int(match.group(1))
            conn_id = int(match.group(2))
            mac = match.group(3)
            data_len = int(match.group(4))
            rx_count = int(match.group(5))
            
            devices[mac]['server_index'] = server_index
            devices[mac]['conn_id'] = conn_id
            devices[mac]['rx_counts'].append(rx_count)
            devices[mac]['data_lens'].append(data_len)
            devices[mac]['count'] += 1
    
    return devices, line_count, match_count


def analyze_device(mac, data):
    """分析单个设备的丢包情况"""
    rx_counts = sorted(data['rx_counts'])
    count = data['count']
    data_lens = data['data_lens']
    
    if not rx_counts:
        return None
    
    # 计算期望的rx_count范围
    first_count = rx_counts[0]
    last_count = rx_counts[-1]
    expected = last_count - first_count + 1
    received = count
    lost = expected - received
    
    # 计算连续丢包
    gaps = []
    current_gap_start = None
    current_gap_len = 0
    
    for i in range(1, len(rx_counts)):
        diff = rx_counts[i] - rx_counts[i-1]
        if diff > 1:
            # 发现丢包
            gap_start = rx_counts[i-1] + 1
            gap_end = rx_counts[i] - 1
            gap_len = diff - 1
            gaps.append({
                'start': gap_start,
                'end': gap_end,
                'length': gap_len
            })
    
    max_gap = max([g['length'] for g in gaps]) if gaps else 0
    
    # 计算数据量
    total_bytes = sum(data_lens)
    avg_len = total_bytes / count if count > 0 else 0
    
    return {
        'mac': mac,
        'server_index': data['server_index'],
        'conn_id': data['conn_id'],
        'first_rx_count': first_count,
        'last_rx_count': last_count,
        'expected': expected,
        'received': received,
        'lost': lost,
        'loss_rate': (lost / expected * 100) if expected > 0 else 0,
        'max_gap': max_gap,
        'gaps': gaps,
        'total_bytes': total_bytes,
        'avg_packet_len': avg_len
    }


def print_device_result(result):
    """打印单个设备的分析结果"""
    print(f"\n设备 {result['mac']}:")
    print(f"  服务器索引:   {result['server_index']}")
    print(f"  连接ID:       {result['conn_id']}")
    print(f"  rx_count范围: {result['first_rx_count']} - {result['last_rx_count']}")
    print(f"  期望接收:     {result['expected']}")
    print(f"  实际接收:     {result['received']}")
    print(f"  丢包数:       {result['lost']}")
    print(f"  丢包率:       {result['loss_rate']:.2f}%")
    print(f"  最大连续丢包: {result['max_gap']}")
    print(f"  总数据量:     {result['total_bytes']} 字节")
    print(f"  平均包长:     {result['avg_packet_len']:.1f} 字节")
    
    # 打印丢包详情（最多显示前10个）
    if result['gaps']:
        print(f"  丢包详情 (前10个):")
        for i, gap in enumerate(result['gaps'][:10]):
            print(f"    #{i+1}: rx_count {gap['start']}-{gap['end']} ({gap['length']}包)")
        if len(result['gaps']) > 10:
            print(f"    ... 还有 {len(result['gaps'])-10} 个丢包段")


def main():
    # 解析参数
    if len(sys.argv) < 2:
        print("用法: py -3 analyze_log.py <日志文件路径>")
        print("      adb shell 'cat /tmp/sle_app.log' | python3 analyze_log.py -")
        sys.exit(1)
    
    log_source = sys.argv[1]
    
    print("=" * 70)
    print("SLE网关性能测试 - 日志分析")
    print("=" * 70)
    print(f"日志来源: {log_source}")
    print("=" * 70)
    
    # 解析日志
    devices, line_count, match_count = parse_log(log_source)
    
    if not devices:
        print("\n未找到有效的 [SLE][RX] 日志行")
        print("请确保:")
        print("  1. sle_data_app 正在运行")
        print("  2. 有DTU设备连接并发送数据")
        print("  3. 日志文件路径正确")
        return
    
    print(f"\n日志统计:")
    print(f"  总行数:     {line_count}")
    print(f"  匹配行数:   {match_count}")
    print(f"  设备数量:   {len(devices)}")
    
    # 分析每个设备
    results = []
    for mac, data in devices.items():
        result = analyze_device(mac, data)
        if result:
            results.append(result)
            print_device_result(result)
    
    # 汇总统计
    print("\n" + "=" * 70)
    print("汇总统计")
    print("=" * 70)
    
    total_expected = sum(r['expected'] for r in results)
    total_received = sum(r['received'] for r in results)
    total_lost = sum(r['lost'] for r in results)
    total_bytes = sum(r['total_bytes'] for r in results)
    
    overall_loss_rate = (total_lost / total_expected * 100) if total_expected > 0 else 0
    
    print(f"\n设备数:     {len(results)}")
    print(f"总期望接收: {total_expected}")
    print(f"总实际接收: {total_received}")
    print(f"总丢包数:   {total_lost}")
    print(f"总丢包率:   {overall_loss_rate:.2f}%")
    print(f"总数据量:   {total_bytes} 字节 ({total_bytes/1024:.1f} KB)")
    
    # 每个设备的简要信息
    print(f"\n设备列表:")
    for r in results:
        print(f"  {r['mac']}: 丢包率={r['loss_rate']:.2f}%, 接收={r['received']}/{r['expected']}")
    
    print("=" * 70)
    
    # 输出CSV格式结果（便于后续处理）
    print("\n[CSV格式结果]")
    print("mac,server_index,conn_id,expected,received,lost,loss_rate,total_bytes")
    for r in results:
        print(f"{r['mac']},{r['server_index']},{r['conn_id']},{r['expected']},"
              f"{r['received']},{r['lost']},{r['loss_rate']:.2f},{r['total_bytes']}")


if __name__ == '__main__':
    main()
