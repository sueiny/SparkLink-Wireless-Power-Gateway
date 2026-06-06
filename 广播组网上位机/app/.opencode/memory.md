# 项目规则与上下文

## 测试结果管理规则
- 没有测试完整（测试结果汇报.md）的结果目录不保留，及时清理
- 生成测试报告使用 tools/regenerate_report.py

## 项目关键信息
- 网关地址: 0x00
- Dongle 地址: 0x10 (16 decimal), 串口 /dev/ttyUSB0, CH340 芯片
- SEND 格式: 目标=十进制(1-10), 路径=十六进制(00-0A)
- 单次发送模式: --send-mode single_send
- 纯净 Dijkstra 模式: --route-mode sample_dijkstra (边权恒定 0.005, 最小跳数)
- RSSI 采集参数: 硬编码 rssi_seconds=20, rssi_requests=10

## 当前最优参数 (sample_dijkstra)
```
--rounds 5 --send-mode single_send --no-retry --ack-timeout 5.0 --hop-penalty 0 --interval 2.0 --route-mode sample_dijkstra
```

## 节点 4 问题
- 节点 4 只有网关→4 的入边 (RSSI -84), 无其他节点→4 的边
- 物理位置/天线方向问题, 信号单向可达
- 需要移动节点 4 或检查硬件

## 暂停功能 (--enable-pause)
- 按 s: 暂停 (当前 round 完成后生效)
- 按 y: 重新采集 RSSI 并继续
- 最多重试 3 次采集

## Dongle 硬件注意事项
- 偶尔会 USB 断开, 需物理拔插
- 串口断开时代码会保存已有结果 (SerialException 保护)
- 暂停恢复后不使用 reset_input_buffer(), 用 drain + 重试代替

## 测试报告路径
- Dijkstra: logs/dijkstra_hw/第N次测试/
- D3QN: logs/d3qn_hw/第N次测试/
- 报告生成: .venv-d3qn/bin/python tools/regenerate_report.py 第N次测试
