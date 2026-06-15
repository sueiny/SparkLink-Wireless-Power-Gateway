# 物模型 V2 设计文档

## 0. 关键指示

> **云端展示原则**：对用户而言，只有 **root** 和 **node** 两种角色概念。中继、叶子等SLE网络内部概念用户不需要了解，云端统一展示为node。
>
> **在线状态判定**：拓扑路由表中存在的节点即为在线，不存在即为离线。无需额外的在线状态字段，直接根据路由表判断。
>
> **MAC地址废弃**：所有设备不再使用MAC地址标识，统一使用数字ID（1-255）。
>
> **外接设备命名规则**：DTU以外的设备（电表、继电器、温湿度变送器）命名基于 **站号 + 设备类型**。例如：站号=1 + modbus_type=单相电表 → `METER_001`；站号=101 + modbus_type=继电器 → `RELAY_101`。站号范围1-255，足够使用。
>
> **ThingsKit组织ID**：`c754877c-4d8e-4df3-bf23-bb15a0a37890`（DTU组网组织），所有设备创建时必须绑定此组织。
>
> **物模型同步缓存问题**：当物模型同步"成功"但云平台不生效时，需要**删除产品并重建**才能解决缓存问题。流程：删除旧产品 → 重新创建 → 运行同步脚本 → 刷新浏览器。

## 1. 设计原则

- **统一使用ID标识**：所有设备使用数字ID（1-255），不再使用MAC地址
- **命名规范**：DTU节点命名为 `DTU_XXX`（如DTU_001, DTU_012），设备命名为 `METER_XXX`、`ENV_XXX`、`RELAY_XXX`
- **拓扑信息简化**：使用父节点ID和子节点ID列表，便于路由表解析
- **物模型精简**：去除冗余字段，保留必要属性

## 2. 拓扑树结构

当前拓扑以 `app/Gateway/docs/00_项目说明/设备拓扑图.md` 和 `gateway_config.json` 为准，共 31 个 DTU 节点。树1负责外接设备采集；树2负责 SLE 中继节点展示，其中 DTU_023、DTU_026、DTU_029 已并入树2。

### 2.1 树1：外接设备树

```
DTU_001 (root)
├── DTU_002
│   ├── DTU_004
│   └── DTU_005
├── DTU_003
│   ├── DTU_006
│   └── DTU_007
├── DTU_008
├── DTU_009
├── DTU_010
└── DTU_011
```

### 2.2 树2：SLE 中继树

```
DTU_012 (root)        DTU_023 (root)   DTU_026 (root)   DTU_029 (root)
├── DTU_013           ├── DTU_024      ├── DTU_027      ├── DTU_030
│   ├── DTU_016       └── DTU_025      └── DTU_028      └── DTU_031
│   ├── DTU_017
│   └── DTU_018
├── DTU_014
│   ├── DTU_019
│   └── DTU_020
└── DTU_015
    ├── DTU_021
    └── DTU_022
```

**节点统计**：
- 树1：11 个 DTU 节点（DTU_001 ~ DTU_011）
- 树2：20 个 DTU 节点（DTU_012 ~ DTU_031）
- root 节点：5 个（DTU_001、DTU_012、DTU_023、DTU_026、DTU_029）
- node 节点：26 个
- **总计**：31 个 DTU 节点

### 2.3 当前 DTU 路由表

| DTU节点 | parent_id | child_ids | 云端角色 |
|---------|-----------|-----------|----------|
| DTU_001 | 0 | 2,3,8,9,10,11 | root |
| DTU_002 | 1 | 4,5 | node |
| DTU_003 | 1 | 6,7 | node |
| DTU_004 | 2 | - | node |
| DTU_005 | 2 | - | node |
| DTU_006 | 3 | - | node |
| DTU_007 | 3 | - | node |
| DTU_008 | 1 | - | node |
| DTU_009 | 1 | - | node |
| DTU_010 | 1 | - | node |
| DTU_011 | 1 | - | node |
| DTU_012 | 0 | 13,14,15 | root |
| DTU_013 | 12 | 16,17,18 | node |
| DTU_014 | 12 | 19,20 | node |
| DTU_015 | 12 | 21,22 | node |
| DTU_016 | 13 | - | node |
| DTU_017 | 13 | - | node |
| DTU_018 | 13 | - | node |
| DTU_019 | 14 | - | node |
| DTU_020 | 14 | - | node |
| DTU_021 | 15 | - | node |
| DTU_022 | 15 | - | node |
| DTU_023 | 0 | 24,25 | root |
| DTU_024 | 23 | - | node |
| DTU_025 | 23 | - | node |
| DTU_026 | 0 | 27,28 | root |
| DTU_027 | 26 | - | node |
| DTU_028 | 26 | - | node |
| DTU_029 | 0 | 30,31 | root |
| DTU_030 | 29 | - | node |
| DTU_031 | 29 | - | node |

## 3. DTU节点物模型（简化版）

### 3.1 属性列表

| 标识符 | 名称 | 数据类型 | 访问模式 | 说明 |
|--------|------|----------|----------|------|
| role | 节点角色 | ENUM | r | 0=root, 1=node |
| name | 设备名称 | TEXT | r | DTU_XXX格式 |
| online | 在线状态 | BOOL | r | 根据拓扑路由表判断：存在=在线 |
| topology | 拓扑信息 | STRUCT | r | 包含parent_id, child_ids |

> **注意**：online字段由拓扑路由表自动派生，路由表中存在即为在线（true），不存在即为离线（false）。

### 3.2 节点角色枚举

> **云端展示说明**：虽然SLE网络内部有root/relay/leaf等角色区分，但对用户只展示root和node两种。用户无需了解中继、叶子等概念。

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | root | 根节点（parent_id=0，如 DTU_001、DTU_012、DTU_023、DTU_026、DTU_029） |
| 1 | node | 普通节点（所有非root节点，包括中继和叶子） |

### 3.3 拓扑信息结构

```json
{
  "functionName": "拓扑信息",
  "identifier": "topology",
  "specs": {
    "dataType": {
      "type": "STRUCT",
      "specs": [
        {
          "functionName": "父节点ID",
          "identifier": "parent_id",
          "dataType": {"type": "INT", "specs": {}}
        },
        {
          "functionName": "子节点数量",
          "identifier": "child_count",
          "dataType": {"type": "INT", "specs": {"unit": "count"}}
        },
        {
          "functionName": "子节点列表",
          "identifier": "child_ids",
          "dataType": {"type": "TEXT", "specs": {}}
        }
      ]
    }
  }
}
```

### 3.4 命名规则

- **DTU节点**：统一命名为 `DTU_XXX`，`XXX` 为三位数字节点 ID。
- **树1 root**：`DTU_001`
- **树1 node**：`DTU_002` ~ `DTU_011`
- **树2 root**：`DTU_012`、`DTU_023`、`DTU_026`、`DTU_029`
- **树2 node**：`DTU_013` ~ `DTU_022`、`DTU_024`、`DTU_025`、`DTU_027`、`DTU_028`、`DTU_030`、`DTU_031`

### 3.5 完整JSON定义

```json
{
  "properties": [
    {
      "functionName": "节点角色",
      "identifier": "role",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {
          "type": "ENUM",
          "specsList": [
            {"value": 0, "name": "root", "dataType": "ENUM"},
            {"value": 1, "name": "node", "dataType": "ENUM"}
          ]
        }
      }
    },
    {
      "functionName": "设备名称",
      "identifier": "name",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {
          "type": "TEXT",
          "specs": {}
        }
      }
    },
    {
      "functionName": "在线状态",
      "identifier": "online",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {
          "type": "BOOL",
          "specs": {
            "boolClose": "离线",
            "boolOpen": "在线"
          }
        }
      }
    },
    {
      "functionName": "拓扑信息",
      "identifier": "topology",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {
          "type": "STRUCT",
          "specs": [
            {
              "functionName": "父节点ID",
              "identifier": "parent_id",
              "dataType": {"type": "INT", "specs": {}}
            },
            {
              "functionName": "子节点数量",
              "identifier": "child_count",
              "dataType": {"type": "INT", "specs": {"unit": "count"}}
            },
            {
              "functionName": "子节点列表",
              "identifier": "child_ids",
              "dataType": {"type": "TEXT", "specs": {}}
            }
          ]
        }
      }
    }
  ],
  "events": [
    {
      "functionName": "节点离线",
      "identifier": "node_offline",
      "functionType": "events",
      "eventType": "ALERT",
      "outputData": [
        {
          "functionName": "节点ID",
          "identifier": "node_id",
          "dataType": {"type": "INT", "specs": {}}
        }
      ]
    },
    {
      "functionName": "拓扑变更",
      "identifier": "topology_changed",
      "functionType": "events",
      "eventType": "INFO",
      "outputData": [
        {
          "functionName": "父节点ID",
          "identifier": "parent_id",
          "dataType": {"type": "INT", "specs": {}}
        },
        {
          "functionName": "子节点数量",
          "identifier": "child_count",
          "dataType": {"type": "INT", "specs": {"unit": "count"}}
        }
      ]
    }
  ],
  "services": [
    {
      "functionName": "重启DTU",
      "identifier": "reboot",
      "functionType": "services",
      "callType": "ASYNC",
      "inputData": [],
      "outputData": [
        {
          "functionName": "执行结果",
          "identifier": "result",
          "dataType": {
            "type": "ENUM",
            "specsList": [
              {"value": 0, "name": "失败", "dataType": "ENUM"},
              {"value": 1, "name": "成功", "dataType": "ENUM"}
            ]
          }
        }
      ]
    }
  ]
}
```

## 4. 单相电表物模型

### 4.1 属性列表

| 标识符 | 名称 | 数据类型 | 访问模式 | 说明 |
|--------|------|----------|----------|------|
| dtu_id | DTU挂载ID | INT | r | 挂载的DTU节点ID |
| voltage | 电压 | DOUBLE | r | 单位：V |
| current | 电流 | DOUBLE | r | 单位：A |
| active_power | 有功功率 | DOUBLE | r | 单位：W |
| power_factor | 功率因数 | DOUBLE | r | 无单位 |
| frequency | 频率 | DOUBLE | r | 单位：Hz |
| energy | 总有功电能 | DOUBLE | r | 单位：kWh |
| relay_status | 拉合闸状态 | ENUM | rw | 0=拉闸, 1=合闸 |
| meter_role | 电表角色 | ENUM | r | 0=支表, 1=总表 |
| parent_meter_id | 父级电表ID | INT | r | 上级电表ID |
| branch_power_sum | 支表功率合计 | DOUBLE | r | 单位：W |
| power_loss | 线损功率 | DOUBLE | r | 单位：W |
| loss_rate | 线损率 | DOUBLE | r | 单位：% |
| online | 在线状态 | BOOL | r | true=在线, false=离线 |

### 4.2 完整JSON定义

```json
{
  "properties": [
    {
      "functionName": "DTU挂载ID",
      "identifier": "dtu_id",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "INT", "specs": {}}
      }
    },
    {
      "functionName": "电压",
      "identifier": "voltage",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "V"}}
      }
    },
    {
      "functionName": "电流",
      "identifier": "current",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "A"}}
      }
    },
    {
      "functionName": "有功功率",
      "identifier": "active_power",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "W"}}
      }
    },
    {
      "functionName": "功率因数",
      "identifier": "power_factor",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {}}
      }
    },
    {
      "functionName": "频率",
      "identifier": "frequency",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "Hz"}}
      }
    },
    {
      "functionName": "总有功电能",
      "identifier": "energy",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "kWh"}}
      }
    },
    {
      "functionName": "拉合闸状态",
      "identifier": "relay_status",
      "functionType": "properties",
      "accessMode": "rw",
      "specs": {
        "dataType": {
          "type": "ENUM",
          "specsList": [
            {"value": 0, "name": "拉闸", "dataType": "ENUM"},
            {"value": 1, "name": "合闸", "dataType": "ENUM"}
          ]
        }
      }
    },
    {
      "functionName": "电表角色",
      "identifier": "meter_role",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {
          "type": "ENUM",
          "specsList": [
            {"value": 0, "name": "支表", "dataType": "ENUM"},
            {"value": 1, "name": "总表", "dataType": "ENUM"}
          ]
        }
      }
    },
    {
      "functionName": "父级电表ID",
      "identifier": "parent_meter_id",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "INT", "specs": {}}
      }
    },
    {
      "functionName": "支表功率合计",
      "identifier": "branch_power_sum",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "W"}}
      }
    },
    {
      "functionName": "线损功率",
      "identifier": "power_loss",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "W"}}
      }
    },
    {
      "functionName": "线损率",
      "identifier": "loss_rate",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "%"}}
      }
    },
    {
      "functionName": "在线状态",
      "identifier": "online",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {
          "type": "BOOL",
          "specs": {
            "boolClose": "离线",
            "boolOpen": "在线"
          }
        }
      }
    }
  ],
  "events": [
    {
      "functionName": "过压告警",
      "identifier": "over_voltage",
      "functionType": "events",
      "eventType": "ALERT",
      "outputData": [
        {"functionName": "当前电压", "identifier": "voltage", "dataType": {"type": "DOUBLE", "specs": {"unit": "V"}}},
        {"functionName": "阈值", "identifier": "threshold", "dataType": {"type": "DOUBLE", "specs": {"unit": "V"}}}
      ]
    },
    {
      "functionName": "欠压告警",
      "identifier": "under_voltage",
      "functionType": "events",
      "eventType": "ALERT",
      "outputData": [
        {"functionName": "当前电压", "identifier": "voltage", "dataType": {"type": "DOUBLE", "specs": {"unit": "V"}}},
        {"functionName": "阈值", "identifier": "threshold", "dataType": {"type": "DOUBLE", "specs": {"unit": "V"}}}
      ]
    },
    {
      "functionName": "过流告警",
      "identifier": "over_current",
      "functionType": "events",
      "eventType": "ALERT",
      "outputData": [
        {"functionName": "当前电流", "identifier": "current", "dataType": {"type": "DOUBLE", "specs": {"unit": "A"}}},
        {"functionName": "阈值", "identifier": "threshold", "dataType": {"type": "DOUBLE", "specs": {"unit": "A"}}}
      ]
    }
  ],
  "services": [
    {
      "functionName": "拉合闸控制",
      "identifier": "set_relay",
      "functionType": "services",
      "callType": "ASYNC",
      "inputData": [
        {
          "functionName": "目标状态",
          "identifier": "state",
          "dataType": {
            "type": "ENUM",
            "specsList": [
              {"value": 0, "name": "拉闸", "dataType": "ENUM"},
              {"value": 1, "name": "合闸", "dataType": "ENUM"}
            ]
          }
        }
      ],
      "outputData": [
        {
          "functionName": "执行结果",
          "identifier": "result",
          "dataType": {
            "type": "ENUM",
            "specsList": [
              {"value": 0, "name": "失败", "dataType": "ENUM"},
              {"value": 1, "name": "成功", "dataType": "ENUM"}
            ]
          }
        }
      ]
    },
    {
      "functionName": "电量清零",
      "identifier": "clear_energy",
      "functionType": "services",
      "callType": "ASYNC",
      "inputData": [],
      "outputData": [
        {
          "functionName": "执行结果",
          "identifier": "result",
          "dataType": {
            "type": "ENUM",
            "specsList": [
              {"value": 0, "name": "失败", "dataType": "ENUM"},
              {"value": 1, "name": "成功", "dataType": "ENUM"}
            ]
          }
        }
      ]
    }
  ]
}
```

## 5. 继电器物模型（简化版）

### 5.1 属性列表

| 标识符 | 名称 | 数据类型 | 访问模式 | 说明 |
|--------|------|----------|----------|------|
| dtu_id | DTU挂载ID | INT | r | 挂载的DTU节点ID |
| relay_state | 继电器状态 | ENUM | rw | 0=关, 1=开 |
| online | 在线状态 | BOOL | r | true=在线, false=离线 |

### 5.2 完整JSON定义

```json
{
  "properties": [
    {
      "functionName": "DTU挂载ID",
      "identifier": "dtu_id",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "INT", "specs": {}}
      }
    },
    {
      "functionName": "继电器状态",
      "identifier": "relay_state",
      "functionType": "properties",
      "accessMode": "rw",
      "specs": {
        "dataType": {
          "type": "ENUM",
          "specsList": [
            {"value": 0, "name": "关", "dataType": "ENUM"},
            {"value": 1, "name": "开", "dataType": "ENUM"}
          ]
        }
      }
    },
    {
      "functionName": "在线状态",
      "identifier": "online",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {
          "type": "BOOL",
          "specs": {
            "boolClose": "离线",
            "boolOpen": "在线"
          }
        }
      }
    }
  ],
  "events": [],
  "services": [
    {
      "functionName": "开关控制",
      "identifier": "set_relay",
      "functionType": "services",
      "callType": "ASYNC",
      "inputData": [
        {
          "functionName": "目标状态",
          "identifier": "state",
          "dataType": {
            "type": "ENUM",
            "specsList": [
              {"value": 0, "name": "关", "dataType": "ENUM"},
              {"value": 1, "name": "开", "dataType": "ENUM"}
            ]
          }
        }
      ],
      "outputData": [
        {
          "functionName": "执行结果",
          "identifier": "result",
          "dataType": {
            "type": "ENUM",
            "specsList": [
              {"value": 0, "name": "失败", "dataType": "ENUM"},
              {"value": 1, "name": "成功", "dataType": "ENUM"}
            ]
          }
        }
      ]
    }
  ]
}
```

## 6. 温湿度变送器物模型

### 6.1 属性列表

| 标识符 | 名称 | 数据类型 | 访问模式 | 说明 |
|--------|------|----------|----------|------|
| dtu_id | DTU挂载ID | INT | r | 挂载的DTU节点ID |
| temperature | 温度 | DOUBLE | r | 单位：°C |
| humidity | 湿度 | DOUBLE | r | 单位：%RH |
| online | 在线状态 | BOOL | r | true=在线, false=离线 |

### 6.2 完整JSON定义

```json
{
  "properties": [
    {
      "functionName": "DTU挂载ID",
      "identifier": "dtu_id",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "INT", "specs": {}}
      }
    },
    {
      "functionName": "温度",
      "identifier": "temperature",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "°C"}}
      }
    },
    {
      "functionName": "湿度",
      "identifier": "humidity",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {"type": "DOUBLE", "specs": {"unit": "%RH"}}
      }
    },
    {
      "functionName": "在线状态",
      "identifier": "online",
      "functionType": "properties",
      "accessMode": "r",
      "specs": {
        "dataType": {
          "type": "BOOL",
          "specs": {
            "boolClose": "离线",
            "boolOpen": "在线"
          }
        }
      }
    }
  ],
  "events": [
    {
      "functionName": "高温告警",
      "identifier": "high_temperature",
      "functionType": "events",
      "eventType": "ALERT",
      "outputData": [
        {"functionName": "当前温度", "identifier": "temperature", "dataType": {"type": "DOUBLE", "specs": {"unit": "°C"}}},
        {"functionName": "阈值", "identifier": "threshold", "dataType": {"type": "DOUBLE", "specs": {"unit": "°C"}}}
      ]
    },
    {
      "functionName": "高湿告警",
      "identifier": "high_humidity",
      "functionType": "events",
      "eventType": "ALERT",
      "outputData": [
        {"functionName": "当前湿度", "identifier": "humidity", "dataType": {"type": "DOUBLE", "specs": {"unit": "%RH"}}},
        {"functionName": "阈值", "identifier": "threshold", "dataType": {"type": "DOUBLE", "specs": {"unit": "%RH"}}}
      ]
    }
  ],
  "services": []
}
```

## 7. 网关物模型（保持不变）

网关物模型保持原有设计，不需要修改。

## 8. 数据结构定义

### 8.1 DTU节点信息

```cpp
struct DtuNodeInfo {
    int node_id;                    // 节点ID (1-255)
    std::string name;               // 设备名称，如 "DTU_012"
    bool online;                    // 在线状态（根据拓扑路由表判断）
    int parent_id;                  // 父节点ID (0表示无父节点)
    std::vector<int> child_ids;     // 子节点ID列表
};
```

### 8.2 外接设备信息

> **命名规则**：设备名称 = 设备类型前缀 + 站号（3位补零）
> - 站号1 + 单相电表 → `METER_001`
> - 站号101 + 继电器 → `RELAY_101`
> - 站号5 + 温湿度变送器 → `ENV_005`

```cpp
struct DeviceInfo {
    int station_id;                 // Modbus站号 (1-255)
    int dtu_id;                     // 挂载的DTU节点ID
    DeviceType type;                // 设备类型 (meter/relay/env)
    std::string name;               // 设备名称，如 "METER_001"
    bool online;                    // 在线状态（跟随DTU节点）
    // ... 其他设备特定属性（电压、电流、温度等）
};

// 设备命名函数
std::string generateDeviceName(DeviceType type, int station_id) {
    std::string prefix;
    switch (type) {
        case DeviceType::SinglePhaseMeter: prefix = "METER_"; break;
        case DeviceType::Relay: prefix = "RELAY_"; break;
        case DeviceType::EnvSensor: prefix = "ENV_"; break;
        default: prefix = "DEV_"; break;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%03d", station_id);
    return prefix + buf;
}
```

## 9. 路由表解析

### 9.1 拓扑树解析逻辑

```cpp
// 从拓扑树解析路由表
struct RouteEntry {
    int dest_id;                    // 目标节点ID
    int next_hop_id;                // 下一跳节点ID
    int hop_count;                  // 跳数
};

// 路由表
std::map<int, RouteEntry> route_table;
```

### 9.2 解析示例

对于 DTU_018：
- 目标：18
- 路径：12 → 13 → 18
- 下一跳：13（从 root 12 出发）
- 跳数：2

## 10. 物模型变更对比

| 设备类型 | 删除字段 | 新增字段 | 修改字段 |
|----------|----------|----------|----------|
| DTU节点 | mac, uptime, collect_config | - | role枚举改为root/node, topology改为id格式 |
| 单相电表 | mac, parent_meter_id(TEXT) | dtu_id | parent_meter_id改为INT |
| 继电器 | control_mode, mac | dtu_id | - |
| 温湿度变送器 | mac | dtu_id | - |

## 11. 设备类型与站号映射表

| modbus_type | 设备类型 | 名称前缀 | 示例 |
|-------------|----------|----------|------|
| 0x01 | 三相电表 | METER_ | METER_001 |
| 0x02 | 单相电表 | METER_ | METER_001 |
| 0x03 | 温湿度变送器 | ENV_ | ENV_001 |
| 0x04 | 继电器 | RELAY_ | RELAY_001 |
| 0x05 | 其他预留 | DEV_ | DEV_001 |
