# 物模型与配置对齐 Code Review

> 日期：2026-05-11  
> 关注点：`things_model`、`config/gateway_config.json`、`ConfigManager`、`DeviceModel`

## 1. Review 结论

本阶段已经把 `gateway_config.json` 从旧设备模型切换到当前 ThingsKit 测试模型：

- 7 个单相电表：`METER_001` 到 `METER_007`
- 2 个温湿度变送器：`ENV_001`、`ENV_002`
- 2 个继电器：`RELAY_001`、`RELAY_002`
- 11 个 DTU 节点：`DTU_001` 到 `DTU_011`

配置中已经体现三类关系：

| 关系 | 字段 | 用途 |
|------|------|------|
| 电表业务拓扑 | `parent_meter_id` | 表示总表/支表层级 |
| DTU 通信拓扑 | `mac`、`parent_mac`、`child_macs` | 表示 DTU 节点组网 |
| DTU 采集映射 | `modbus_addr`、`modbus_type` | 表示 DTU 下挂的 Modbus 设备类型 |

## 2. 代码检查

### 2.1 `DeviceInfo` 已补充 DTU 字段

当前 `DeviceInfo` 已包含：

```cpp
std::string mac;
std::string parent_mac;
std::string child_macs;
int modbus_addr = 0;
int modbus_type = 0;
```

学习点：

这些字段没有放到单独的 DTU 类里，是合理的。当前设备配置是统一数组，`DeviceInfo` 作为轻量描述结构，允许不同设备类型只使用自己需要的字段。这样不会过早引入继承层级。

### 2.2 `ConfigManager::parseDevice()` 已读取新增字段

配置读取层已经把 JSON 中的 DTU 字段读入 `DeviceInfo`。

这一步很关键，因为后面的 `MockDataSource` 不应该重新读 JSON，也不应该知道配置文件路径。它只应该消费已经解析好的 `DeviceInfo`。

### 2.3 `meter_role` 已从枚举映射为 ThingsKit BOOL

当前单相电表物模型中：

```text
meter_role: BOOL
boolOpen = 总表
boolClose = 支表
```

代码中使用：

```cpp
bool toThingsKitBool(MeterRole role)
```

这比继续用旧的数值枚举更符合当前物模型。

## 3. 当前模型边界

### 3.1 DTU 拓扑不是电表拓扑

容易误解的地方：

```text
DTU_001 -> DTU_002/DTU_003
METER_001 -> METER_002/METER_003
```

这两个形状看起来相似，但含义不同：

- DTU 树表示无线/通信组网。
- 电表树表示电力业务层级。
- `DTU_001` 与 `METER_001` 是采集映射，不是同一个实体。

### 3.2 `collect_config.type_1` 不能全部写单相电表

当前枚举应保持：

| 值 | 类型 |
|----|------|
| 0 | 未定义 |
| 1 | 三相电表 |
| 2 | 单相电表 |
| 3 | 温湿度变送器 |
| 4 | 继电器 |
| 5 | 其他预留 |

因此：

- `DTU_001` 到 `DTU_007`：`type_1 = 2`
- `DTU_008`、`DTU_009`：`type_1 = 3`
- `DTU_010`、`DTU_011`：`type_1 = 4`

## 4. Review 发现

### 已修正

1. 旧配置中的设备已替换为当前测试拓扑。
2. DTU 节点的 `collect_config` 已能根据下挂设备类型生成。
3. 物模型 README 已说明拓扑差异与脚本边界。

### 仍需注意

1. `gateway_config.json` 中的 Wi-Fi 密码属于运行配置，应避免在对外材料中直接暴露。
2. `modbus_addr` 当前测试都使用 `1`，后续接真实设备时应按现场地址更新。
3. `child_macs` 当前用逗号分隔字符串，满足 ThingsKit TEXT 字段；如果后续平台支持数组，可以再升级结构。

## 5. 验证建议

每次修改 `gateway_config.json` 后都应跑：

```bash
python3 -m json.tool app/Gateway/gatewayd/config/gateway_config.json >/dev/null
```

每次修改模型后应核对：

```text
本地 things_model 字段
        ↓
gatewayd MockDataSource 生成字段
        ↓
ThingsKitMapper 输出字段
        ↓
平台页面物模型字段
```

