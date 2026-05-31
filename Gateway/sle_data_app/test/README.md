# SLE网关性能测试工具

## 测试架构

```
主机 (Windows/Python) --> 串口 --> DTU --SLE--> 网关 (sle_data_app) --> 日志文件
```

## 文件说明

| 文件 | 说明 |
|------|------|
| `send_test.py` | 主机端发送脚本（Windows/Linux通用） |
| `analyze_log.py` | 日志分析脚本（Windows/Linux通用） |
| `run_test.bat` | Windows测试启动脚本 |
| `analyze.bat` | Windows日志分析脚本 |

## Windows使用方法

### 前置条件

1. **安装Python 3.6+**
   - 下载地址: https://www.python.org/downloads/
   - 安装时勾选 "Add Python to PATH"

2. **安装pyserial**
   ```cmd
   pip install pyserial
   ```

3. **安装ADB驱动**（用于从网关获取日志）
   - 下载Android SDK Platform Tools
   - 添加到系统PATH

### 步骤1: 启动网关端

在网关设备上运行sle_data_app：

```bash
# 停止之前的进程
killall sle_data_app
sleep 5

# 启动sle_data_app，日志输出到文件
/userdata/gateway/bin/sle_data_app --config /userdata/gateway/config/sle_data_app.json 2>&1 | tee /tmp/sle_app.log
```

### 步骤2: 主机端发送测试数据

**方法1: 双击运行**
- 双击 `run_test.bat` 使用默认串口（COM22, COM26, COM28）

**方法2: 命令行运行**
```cmd
# 测试单个串口
python send_test.py COM22

# 测试多个串口
python send_test.py COM22 COM26 COM28

# 或使用批处理
run_test.bat COM22 COM26 COM28
```

### 步骤3: 获取并分析日志

**方法1: 使用批处理**
```cmd
# 从网关获取日志
adb pull /tmp/sle_app.log .

# 分析日志
analyze.bat sle_app.log
```

**方法2: 命令行运行**
```cmd
# 从网关获取日志
adb pull /tmp/sle_app.log .

# 分析日志
python analyze_log.py sle_app.log
```

## Linux使用方法

### 步骤1: 启动网关端

```bash
killall sle_data_app; sleep 5
/userdata/gateway/bin/sle_data_app --config /userdata/gateway/config/sle_data_app.json 2>&1 | tee /tmp/sle_app.log
```

### 步骤2: 主机端发送测试数据

```bash
# 测试单个串口
python3 send_test.py /dev/ttyS22

# 测试多个串口
python3 send_test.py /dev/ttyS22 /dev/ttyS26 /dev/ttyS28
```

### 步骤3: 分析日志

```bash
# 从网关获取日志
adb pull /tmp/sle_app.log .

# 分析日志
python3 analyze_log.py sle_app.log

# 或直接从网关读取分析
adb shell "cat /tmp/sle_app.log" | python3 analyze_log.py -
```

## 输出示例

### 发送端输出

```
============================================================
SLE网关性能测试 - 发送端 (Windows)
============================================================
串口列表: COM22, COM26, COM28
波特率:   115200
测试时长: 10秒
数据包大小: 64字节
发送间隔: 0.01秒 (10ms)
预期速率: 100 包/秒/通道
============================================================
[COM22] 串口已打开
[COM22] 开始发送: 时长=10s, 包大小=64B, 间隔=0.01s
[COM22] 发送完成: 1000包, 耗时=10.01s, 速率=99.9pkt/s
[COM22] 串口已关闭

============================================================
发送统计
============================================================
COM22: 1000包
COM26: 1000包
COM28: 1000包
------------------------------------------------------------
总计: 3000包
总耗时: 10.03秒
平均速率: 299.1 包/秒
============================================================
```

### 分析端输出

```
======================================================================
SLE网关性能测试 - 日志分析
======================================================================
日志来源: sle_app.log
======================================================================

日志统计:
  总行数:     3000
  匹配行数:   2955
  设备数量:   3

设备 12:00:00:00:00:a1:
  服务器索引:   0
  连接ID:       0
  rx_count范围: 0 - 999
  期望接收:     1000
  实际接收:     985
  丢包数:       15
  丢包率:       1.50%
  最大连续丢包: 3
  总数据量:     63040 字节
  平均包长:     64.0 字节

======================================================================
汇总统计
======================================================================

设备数:     3
总期望接收: 3000
总实际接收: 2955
总丢包数:   45
总丢包率:   1.50%
总数据量:   188640 字节 (184.2 KB)

设备列表:
  12:00:00:00:00:a1: 丢包率=1.50%, 接收=985/1000
  12:00:00:00:00:a2: 丢包率=2.20%, 接收=978/1000
  12:00:00:00:00:a4: 丢包率=0.80%, 接收=992/1000
======================================================================
```

## 常见问题

### 1. 串口打开失败

**问题**: `serial.serialutil.SerialException: could not open port 'COM22'`

**解决**:
- 检查串口设备是否连接
- 检查串口号是否正确（设备管理器查看）
- 检查是否有其他程序占用串口
- 检查串口权限（Linux: `sudo chmod 666 /dev/ttyS22`）

### 2. pyserial未安装

**问题**: `ModuleNotFoundError: No module named 'serial'`

**解决**:
```cmd
pip install pyserial
```

### 3. 日志分析无结果

**问题**: `未找到有效的 [SLE][RX] 日志行`

**解决**:
- 确保sle_data_app正在运行
- 确保有DTU设备连接并发送数据
- 检查日志文件路径是否正确
- 检查日志格式是否正确

## 配置参数

可在脚本中修改以下参数：

```python
duration = 10        # 测试时长(秒)
packet_size = 64     # 数据包大小(字节)
interval = 0.01      # 发送间隔(秒)
baud_rate = 115200   # 波特率
```
