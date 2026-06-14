# Modbus 寄存器规格（基于实际设备手册）

**版本**: v2.0
**日期**: 2026-06-01
**依据**: 三份实际设备手册
- `2P导轨485电表说明书2026.doc` — 单相导轨电表
- `温湿度采集模块使用手册.pdf` — RS485 温湿度变送器
- `数字量输入输出系列使用手册(RS485).pdf` — RS485 四路继电器模块

---

## 一、单相导轨电表（DDS 系列）

**默认参数**: 地址=0x01, 波特率=9600, 校验=无(N.8.1)
**功能码**: 0x04（读输入寄存器）
**数据格式**: 32 位 IEEE 754 单精度浮点，大端序，每个值占 2 个连续寄存器（4 字节）

### 1.1 浮点型寄存器（功能码 0x04）

| HEX 地址 | 寄存器号 | 字段 | 数据格式 | 单位 | 说明 |
|----------|---------|------|---------|------|------|
| 0x012C | 301 | voltage | Float32 | V | 电压，如 220.1 |
| 0x012E | 303 | current | Float32 | A | 电流，如 5.0 |
| 0x0130 | 305 | active_power | Float32 | W | 瞬时有功功率，如 1096.0 |
| 0x0132 | 307 | power_factor | Float32 | cosΦ | 功率因数，如 1.0 |
| 0x0134 | 309 | frequency | Float32 | Hz | 电网频率，如 50.0 |
| 0x0136 | 311 | energy | Float32 | kWh | 总有功电能，如 13.09 |
| 0x0138 | 313 | relay_status | Float32 | - | 拉合闸状态：85.0=合闸，170.0=拉闸 |
| 0x013A | 315 | baudrate | Float32 | - | 波特率 |
| 0x013C | 317 | parity | Float32 | - | 校验位 |
| 0x013E | 319 | address | Float32 | - | 通讯地址 |

**一次读取全部参数**：从 0x012C 开始读 14 个寄存器（0x000E），返回 28 字节。

**示例帧**：
```
请求: 01 04 01 2C 00 0E B1 FB
响应: 01 04 1C
      43 5C 00 00    → voltage = 220.0V
      40 A0 39 58    → current = 5.01A
      44 89 19 9A    → active_power = 1096.4W
      3F 80 00 00    → power_factor = 1.0
      42 48 00 00    → frequency = 50.0Hz
      41 51 70 A4    → energy = 13.09kWh
      42 AA 00 00    → relay_status = 85.0 (合闸)
      97 1C          → CRC16
```

### 1.2 整型寄存器（功能码 0x04）— 推荐

| HEX 地址 | 寄存器号 | 字段 | 数据格式 | 缩放 | 单位 |
|----------|---------|------|---------|------|------|
| 0x0190 | 401 | voltage | Int16 | ×0.1 | V |
| 0x0191 | 402 | current | Int16 | ×0.01 | A |
| 0x0192 | 403 | active_power | Int16 | ×1 | W |
| 0x0193 | 404 | power_factor | Int16 | ×0.001 | cosΦ |
| 0x0194 | 405 | frequency | Int16 | ×0.01 | Hz |
| 0x0195 | 406 | energy | Int32 | ×0.01 | kWh |
| 0x0197 | 408 | relay_status | Int16 | - | 0xAA=拉闸, 0x55=合闸 |
| 0x0198 | 409 | baudrate | Int16 | - | - |
| 0x0199 | 410 | parity | Int16 | - | 0=E.8.1, 1=O.8.1, 2=N.8.1 |
| 0x019A | 411 | address | Int16 | - | 1-247 |

> 注：0x0190 范围为输入寄存器，使用功能码 0x04 读取。

### 1.3 写寄存器（功能码 0x10）

| HEX 地址 | 寄存器号 | 字段 | 数据格式 | 说明 |
|----------|---------|------|---------|------|
| 0x0008 | 9 | address | Float32 | 通讯地址 1-247 |
| 0x0000 | 1 | baudrate | Float32 | 波特率（1200/2400/4800/9600 转浮点写入） |
| 0x0010 | 17 | relay_control | Int16 | 0xAAAA=拉闸, 0x5555=合闸 |
| 0x0020 | 33 | energy_clear | Int16 | 写入 0x5555 清零总电量 |

### 1.4 仿真建议

**推荐使用整型寄存器（功能码 0x04）**，原因：
- 数据量小（每个值 1-2 个寄存器）
- 解析简单（整数 + 缩放因子）
- 与 gatewayd 的 `numeric_values` 映射直接

**读取帧**：从 0x0190 开始读 11 个寄存器
```
请求: 01 04 01 90 00 0B XX XX
响应: 01 04 16
      XX XX       → voltage (×0.1)
      XX XX       → current (×0.01)
      XX XX       → active_power
      XX XX       → power_factor (×0.001)
      XX XX       → frequency (×0.01)
      XX XX XX XX → energy (Int32, ×0.01)
      XX XX       → relay_status
      XX XX       → baudrate
      XX XX       → parity
      XX XX       → address
      XX XX       → CRC16
```

---

## 二、RS485 温湿度变送器（Waveshare）

**默认参数**: 地址=0x01, 波特率=9600
**功能码**: 0x03（读保持寄存器）或 0x04（读输入寄存器）
**数据格式**: 16 位整数

### 2.1 寄存器映射

| 地址 | 字段 | 数据格式 | 缩放 | 单位 | 说明 |
|------|------|---------|------|------|------|
| 0x0000 | humidity | uint16 | ×0.1 | %RH | 湿度，如 655 = 65.5% |
| 0x0001 | temperature | int16 | ×0.1 | °C | 温度，如 258 = 25.8°C，负值表示零下 |
| 0x0002 | device_addr | uint16 | - | - | 设备地址 |
| 0x0003 | baudrate | uint16 | - | - | 波特率：0=2400, 1=4800, 2=9600, 3=115200 |

### 2.2 示例帧

```
请求: 01 03 00 00 00 02 C5 CB
响应: 01 03 04 02 8F 01 02 XX XX
      02 8F    → humidity = 655 (=65.5%RH)
      01 02    → temperature = 258 (=25.8°C)
      XX XX    → CRC16
```

### 2.3 仿真建议

**读取帧**：读 2 个寄存器
```
请求: 01 03 00 00 00 02 C5 CB
```

---

## 三、RS485 四路继电器模块

**默认参数**: 地址=0x01, 波特率=9600
**功能码**: 0x05（写单个线圈）, 0x01（读线圈状态）

### 3.1 线圈地址

| 地址 | 字段 | 说明 |
|------|------|------|
| 0x0000 | relay_1 | 第 1 路继电器（0=断开, 1=闭合） |
| 0x0001 | relay_2 | 第 2 路继电器 |
| 0x0002 | relay_3 | 第 3 路继电器 |
| 0x0003 | relay_4 | 第 4 路继电器 |

### 3.2 读取帧（功能码 0x01）

```
请求: 01 01 00 00 00 04 3D C9
响应: 01 01 01 0X XX XX
      0X    → 低 4 位分别为 4 路继电器状态
      XX XX → CRC16
```

### 3.3 写入帧（功能码 0x05）

```
闭合第 1 路: 01 05 00 00 FF 00 8C 3A
断开第 1 路: 01 05 00 00 00 00 CD CA
```

### 3.4 设备地址修改

功能码 0x06，地址 0x4000：
```
改地址为 2: 01 06 40 00 00 02 XX XX
```

### 3.5 仿真建议

**读取帧**：读 4 个线圈
```
请求: 01 01 00 00 00 04 3D C9
```

---

## 四、gatewayd 解析器映射表

### 4.1 电表 → TelemetryData

| Modbus 字段 | TelemetryData key | 类型 | 转换 |
|-------------|-------------------|------|------|
| voltage | numeric_values["voltage"] | double | raw_int × 0.1 |
| current | numeric_values["current"] | double | raw_int × 0.01 |
| active_power | numeric_values["active_power"] | double | 直接使用 |
| power_factor | numeric_values["power_factor"] | double | raw_int × 0.001 |
| frequency | numeric_values["frequency"] | double | raw_int × 0.01 |
| energy | numeric_values["energy"] | double | raw_int32 × 0.01 |
| relay_status | integer_values["relay_status"] | int64 | 0x55=1(合闸), 0xAA=0(拉闸) |

### 4.2 温湿度 → TelemetryData

| Modbus 字段 | TelemetryData key | 类型 | 转换 |
|-------------|-------------------|------|------|
| humidity | numeric_values["humidity"] | double | raw_int × 0.1 |
| temperature | numeric_values["temperature"] | double | raw_int × 0.1 |

### 4.3 继电器 → TelemetryData

| Modbus 字段 | TelemetryData key | 类型 | 转换 |
|-------------|-------------------|------|------|
| relay_1 | integer_values["relay_state"] | int64 | 0 或 1 |

---

## 五、DTU 采集配置

每个 DTU 的 `collect_config` 定义了该 DTU 下挂的 Modbus 设备：

| DTU | modbus_addr | modbus_type | 读取命令 | 说明 |
|-----|-------------|-------------|---------|------|
| DTU_001~007 | 1 | 2 (电表) | `01 04 01 90 00 0B XX XX` | 读整型寄存器 11 个 (功能码0x04) |
| DTU_008~009 | 1 | 3 (温湿度) | `01 03 00 00 00 02 XX XX` | 读 2 个寄存器 |
| DTU_010~011 | 1 | 4 (继电器) | `01 01 00 00 00 04 XX XX` | 读 4 个线圈 |

---

## 六、与 MockDataSource 的对齐关系

| MockDataSource 生成的字段 | 对应 Modbus 寄存器 | 缩放 |
|--------------------------|-------------------|------|
| voltage: 220.0 ± 1.8 | 电表 0x0190 | ×0.1 → 2182~2218 |
| current: 2.4 ~ 6.6 | 电表 0x0191 | ×0.01 → 240~660 |
| active_power: ~500W | 电表 0x0192 | 直接 → ~500 |
| power_factor: 0.94 ± 0.02 | 电表 0x0193 | ×0.001 → 920~960 |
| frequency: 50.0 ± 0.03 | 电表 0x0194 | ×0.01 → 4997~5003 |
| energy: 累加 | 电表 0x0195-0196 | ×0.01 → 累加值×100 |
| temperature: 28.0 ± 1.6 | 温湿度 0x0001 | ×0.1 → 264~296 |
| humidity: 60.0 ± 3.2 | 温湿度 0x0000 | ×0.1 → 568~632 |
| relay_state: 0/1 | 继电器 0x0000 | 直接 0 或 1 |

---

**文档维护者**: Gateway 开发团队
**最后更新**: 2026-06-01
