# Gateway 新物模型设计

## 1. 设计原则

- **统一使用数字ID**：DTU节点和设备均使用 1-255 的数字ID标识，不再使用MAC地址
- **简化存储**：物模型只保留必要字段
- **拓扑用ID**：父节点、子节点均使用ID而非MAC
- **命名规则**：`DTU_XXX`（如 DTU_001、DTU_101）

## 2. ID编码规则

### 2.1 DTU节点ID

| 范围 | 含义 | 示例 |
|------|------|------|
| 1-99 | Root节点及其子树 | Root 1 下的 DTU: 1-33, Root 2 下的 DTU: 34-66, Root 3 下的 DTU: 67-99 |
| 100-199 | 扩展预留 | - |
| 200-255 | 特殊用途 | 255=广播 |

### 2.2 设备名称规则

```
DTU_{id:03d}
```

示例：
- ID=1 → DTU_001
- ID=36 → DTU_036
- ID=101 → DTU_101

## 3. 物模型定义

### 3.1 DTU节点物模型

DTU节点物模型只保留4个属性：

| 标识符 | 名称 | 类型 | 说明 |
|--------|------|------|------|
| `role` | 节点角色 | ENUM | 0=Root, 1=Relay, 2=Leaf, 3=Gateway |
| `name` | 设备名称 | TEXT | 格式: `DTU_{id:03d}` |
| `online` | 在线状态 | BOOL | true=在线, false=离线 |
| `topology` | 拓扑信息 | STRUCT | 包含 parent_id 和 child_ids |

### 3.2 拓扑信息 STRUCT

```json
{
  "functionName": "拓扑信息",
  "identifier": "topology",
  "specs": {
    "dataType": {
      "type": "STRUCT",
      "specs": [
        {"functionName": "父节点ID", "identifier": "parent_id", "dataType": {"type": "INT"}},
        {"functionName": "子节点列表", "identifier": "child_ids", "dataType": {"type": "TEXT"}}
      ]
    }
  }
}
```

**字段说明**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `parent_id` | INT | 父节点ID，0表示无父节点（根节点） |
| `child_ids` | TEXT | 子节点ID列表，逗号分隔，如 `"38,48,63"` |

## 4. 数据结构定义

### 4.1 C++ 结构体

```cpp
// DTU 节点物模型
struct DtuNodeModel {
    uint8_t id;                    // 节点ID (1-255)
    uint8_t role;                  // 角色: 0=Root, 1=Relay, 2=Leaf, 3=Gateway
    char name[16];                 // 设备名称: "DTU_001"
    bool online;                   // 在线状态
    uint8_t parent_id;             // 父节点ID, 0=无父节点
    uint8_t child_ids[8];          // 子节点ID列表
    uint8_t child_count;           // 子节点数量
};

// 设备物模型（电表、变送器、继电器等）
struct DeviceModel {
    uint8_t dtu_id;                // 所属DTU节点ID
    uint8_t device_type;           // 设备类型: 0x01=三相电表, 0x02=单相电表, etc.
    uint8_t modbus_addr;           // Modbus地址
    char name[16];                 // 设备名称
    bool online;                   // 在线状态
};
```

### 4.2 拓扑树存储格式

```cpp
// 路由表条目
struct RouteEntry {
    uint8_t node_id;               // 节点ID
    uint8_t next_hop;              // 下一跳节点ID (SLE转发用)
    uint8_t hop_count;             // 跳数
};

// 路由表
struct RouteTable {
    uint8_t root_count;            // Root节点数量
    RouteEntry entries[256];       // 路由条目
};
```

## 5. 拓扑树示例

### 5.1 输入格式（SLE树输出）

```
|- 36
|  |- 39
|  |  |- 38
|  |  |- 48
|  |  |  |- 32
|  |  |  |- 47
|  |  |  |- 54
|  |  |  |- 57
|  |  |  `- 68
|  |  `- 63
|  `- 42
|     |- 43
|     |  |- 34
|     |  `- 61
|     `- 58
|        |- 50
|        `- 52
|- 37
|  |- 23
|  |- 27
|  |  |- 59
|  |  `- 60
|  `- 46
|     `- 45
|- 41
|  |- 33
|  |  `- 26
|  |- 35
|  |  `- 31
|  `- 67
|     |- 53
|     |- 56
|     `- 69
|- 44
|  |- 22
|  |  |- 55
|  |  |- 62
|  |  `- 65
|  |- 24
|  |  |- 28
|  |  |- 49
|  |  |- 51
|  |  `- 66
|  `- 25
`- 64
   `- 40
      `- 30
```

### 5.2 解析后的路由表

**Root节点**（顶级节点，role=0）：

| ID | parent_id | child_ids |
|----|-----------|-----------|
| 36 | 0 | 39,42 |
| 37 | 0 | 23,27,46 |
| 41 | 0 | 33,35,67 |
| 44 | 0 | 22,24,25 |
| 64 | 0 | 40 |

**Relay节点**（中间节点，role=1）：

| ID | parent_id | child_ids |
|----|-----------|-----------|
| 39 | 36 | 38,48,63 |
| 48 | 39 | 32,47,54,57,68 |
| 42 | 36 | 43,58 |
| 43 | 42 | 34,61 |
| 58 | 42 | 50,52 |
| 27 | 37 | 59,60 |
| 46 | 37 | 45 |
| 33 | 41 | 26 |
| 35 | 41 | 31 |
| 67 | 41 | 53,56,69 |
| 22 | 44 | 55,62,65 |
| 24 | 44 | 28,49,51,66 |
| 40 | 64 | 30 |

**Leaf节点**（叶子节点，role=2）：

| ID | parent_id | child_ids |
|----|-----------|-----------|
| 38 | 39 | - |
| 32 | 48 | - |
| 47 | 48 | - |
| 54 | 48 | - |
| 57 | 48 | - |
| 68 | 48 | - |
| 63 | 39 | - |
| 34 | 43 | - |
| 61 | 43 | - |
| 50 | 58 | - |
| 52 | 58 | - |
| 23 | 37 | - |
| 59 | 27 | - |
| 60 | 27 | - |
| 45 | 46 | - |
| 26 | 33 | - |
| 31 | 35 | - |
| 53 | 67 | - |
| 56 | 67 | - |
| 69 | 67 | - |
| 55 | 22 | - |
| 62 | 22 | - |
| 65 | 22 | - |
| 28 | 24 | - |
| 49 | 24 | - |
| 51 | 24 | - |
| 66 | 24 | - |
| 25 | 44 | - |
| 30 | 40 | - |

## 6. ThingsKit 物模型 JSON

### 6.1 DTU节点物模型

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
            {"value": 0, "name": "Root", "dataType": "ENUM"},
            {"value": 1, "name": "Relay", "dataType": "ENUM"},
            {"value": 2, "name": "Leaf", "dataType": "ENUM"},
            {"value": 3, "name": "Gateway", "dataType": "ENUM"}
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
            {"functionName": "父节点ID", "identifier": "parent_id", "dataType": {"type": "INT", "specs": {}}},
            {"functionName": "子节点列表", "identifier": "child_ids", "dataType": {"type": "TEXT", "specs": {}}}
          ]
        }
      }
    }
  ]
}
```

### 6.2 示例数据上报

```json
{
  "DTU_036": {
    "role": 0,
    "name": "DTU_036",
    "online": true,
    "topology": {
      "parent_id": 0,
      "child_ids": "39,42"
    }
  },
  "DTU_039": {
    "role": 1,
    "name": "DTU_039",
    "online": true,
    "topology": {
      "parent_id": 36,
      "child_ids": "38,48,63"
    }
  },
  "DTU_038": {
    "role": 2,
    "name": "DTU_038",
    "online": true,
    "topology": {
      "parent_id": 39,
      "child_ids": ""
    }
  }
}
```

## 7. 与旧物模型对比

| 字段 | 旧版 | 新版 |
|------|------|------|
| 标识 | device_id (字符串) | id (数字 1-255) |
| MAC | mac, parent_mac, child_macs | 移除，用ID替代 |
| 拓扑 | parent_mac, child_macs (字符串) | parent_id, child_ids (数字/逗号分隔数字) |
| 角色 | MeterRole (Main/Branch/None) | role (Root/Relay/Leaf/Gateway) |
| 设备类型 | DeviceType (枚举) | device_type (数字 0x01-0x05) |

## 8. 实现要点

### 8.1 拓扑树解析

```cpp
// 解析 SLE 树输出，构建路由表
bool parseTopologyTree(const char* tree_output, DtuNodeModel nodes[], int* count);

// 根据 ID 获取节点
DtuNodeModel* getNodeById(uint8_t id);

// 获取下一跳（用于SLE转发）
uint8_t getNextHop(uint8_t src_id, uint8_t dst_id);
```

### 8.2 数据上报

```cpp
// 构建 ThingsKit 上报数据
nlohmann::json buildDtuTelemetry(const DtuNodeModel& node);

// 批量上报所有节点
nlohmann::json buildAllDtuTelemetry(const DtuNodeModel nodes[], int count);
```

### 8.3 存储优化

- SQLite 表结构：`dtu_nodes(id PRIMARY KEY, role, name, online, parent_id, child_ids)`
- 不再存储 MAC 地址
- 拓扑变更时只更新 `parent_id` 和 `child_ids` 字段
