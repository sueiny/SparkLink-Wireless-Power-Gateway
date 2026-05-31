# JSON 学习笔记：从配置文件到 C++ nlohmann::json

> 适用场景：嵌入式网关配置、设备数据上报、C++ 读取 JSON 配置文件、ThingsKit/MQTT payload 构造。  
> 学习目标：看懂 JSON、会改 JSON、会用 C++ 解析 JSON、理解 `json.hpp` 和 `nlohmann::json`。

---

# 1. JSON 是什么

**JSON** 全称是 **JavaScript Object Notation**，是一种轻量级数据格式。

它常用于：

- 配置文件
- 网络通信数据
- MQTT payload
- HTTP API 请求/响应
- 设备物模型数据
- 网关参数保存

你的网关配置 JSON 本质上就是：

```json
{
  "gateway": {},
  "thingskit": {},
  "network": {},
  "publish": {},
  "log": {},
  "devices": [],
  "mock": {}
}
```

## 重点

> **JSON 不是 C++ 对象，也不是 map。JSON 是一种数据格式。**  
> C++ 里可以用 `nlohmann::json` 这种类型来表示 JSON 数据。

---

# 2. JSON 的 6 种核心数据类型

JSON 里常见的数据类型有：

| JSON 类型 | 示例 | 含义 |
|---|---|---|
| object | `{ "name": "DTU" }` | 对象，键值对集合 |
| array | `[1, 2, 3]` | 数组，有顺序的数据集合 |
| string | `"DTU_001"` | 字符串 |
| number | `220.5` | 数字，包括整数和浮点数 |
| boolean | `true` / `false` | 布尔值 |
| null | `null` | 空值 |

## 重点

> **JSON 的最外层不一定非得是 `{}` 对象。**  
> 下面这些都是合法 JSON 值：

```json
220.5
```

```json
"hello"
```

```json
true
```

```json
["wifi", "ethernet"]
```

```json
{
  "voltage": 220.5
}
```

---

# 3. JSON 对象 `{}`

JSON 对象由键值对组成：

```json
{
  "name": "dtu网关",
  "version": "1.0.0"
}
```

每一项都是：

```json
"key": value
```

例如：

```json
"name": "dtu网关"
```

表示：

| key | value |
|---|---|
| name | dtu网关 |

## 重点

> **JSON 对象的 key 必须用英文双引号。**

正确：

```json
{
  "name": "dtu网关"
}
```

错误：

```json
{
  name: "dtu网关"
}
```

错误：

```json
{
  'name': "dtu网关"
}
```

---

# 4. JSON 数组 `[]`

数组表示一组有顺序的数据。

例如网络优先级：

```json
{
  "priority": ["ethernet", "wifi", "cellular"]
}
```

含义：

1. 优先使用 ethernet
2. 然后使用 wifi
3. 最后使用 cellular

数组里可以放字符串：

```json
["ethernet", "wifi", "cellular"]
```

也可以放对象：

```json
[
  {
    "device_id": "METER_001",
    "type": "single_phase_meter"
  },
  {
    "device_id": "ENV_001",
    "type": "env_sensor"
  }
]
```

## 重点

> **数组里可以放字符串、数字、布尔、对象，甚至还可以放数组。**

---

# 5. JSON number：整数和浮点数

JSON 里的数字不用加引号。

```json
{
  "port": 11883,
  "interval_ms": 5000,
  "voltage_base": 220.0,
  "temperature": 28.6
}
```

| 字段 | 值 | 类型倾向 |
|---|---:|---|
| `port` | `11883` | 整数 |
| `interval_ms` | `5000` | 整数 |
| `voltage_base` | `220.0` | 浮点数 |
| `temperature` | `28.6` | 浮点数 |

## 正确写法

```json
"voltage": 220.5
```

## 错误写法

```json
"voltage": "220.5"
```

第二种写法把数字变成了字符串。

## 重点

> **`220.5` 在 JSON 里就是 number 值，不是 `{220.5}`，也不是 `{ "value": 220.5 }`。**

---

# 6. JSON 字符串

字符串必须用英文双引号。

```json
{
  "device_id": "DTU_001",
  "host": "thingskit.aiotcomm.com.cn",
  "mode": "wifi"
}
```

## 重点

> **JSON 字符串只能用双引号，不能用单引号。**

正确：

```json
"wifi"
```

错误：

```json
'wifi'
```

---

# 7. JSON 布尔值

JSON 布尔值是：

```json
true
false
```

例如：

```json
{
  "enable": true,
  "enable_cache": false
}
```

## 重点

> **JSON 的布尔值必须小写。**

正确：

```json
true
```

错误：

```json
True
```

错误：

```json
FALSE
```

---

# 8. JSON null

`null` 表示没有值。

例如：

```json
{
  "parent_meter_id": null
}
```

和空字符串不同：

```json
{
  "parent_meter_id": ""
}
```

| 写法 | 含义 |
|---|---|
| `null` | 没有值 |
| `""` | 有值，但是是空字符串 |

## 重点

> **表示“没有父节点”时，`null` 通常比空字符串更语义清晰。**

---

# 9. JSON 语法规则

## 9.1 键值对之间用逗号

正确：

```json
{
  "name": "dtu网关",
  "version": "1.0.0"
}
```

错误：

```json
{
  "name": "dtu网关"
  "version": "1.0.0"
}
```

## 9.2 最后一项后面不能加逗号

正确：

```json
{
  "name": "dtu网关",
  "version": "1.0.0"
}
```

错误：

```json
{
  "name": "dtu网关",
  "version": "1.0.0",
}
```

## 9.3 JSON 标准不支持注释

错误：

```json
{
  "mode": "wifi" // 当前使用 WiFi
}
```

如果需要说明，可以写文档，或者增加说明字段：

```json
{
  "mode": "wifi",
  "mode_desc": "当前使用 WiFi"
}
```

## 重点

> **JSON 最常见报错：少逗号、多逗号、用了单引号、写了注释。**

---

# 10. C++ 里为什么需要 JSON 库

C++ 标准库本身没有内置 JSON 类型。

所以如果要在 C++ 里解析：

```json
{
  "deviceId": "DTU_001",
  "voltage": 220.5,
  "online": true
}
```

就需要第三方库。

常见选择：

| 库 | 语言 | 特点 |
|---|---|---|
| cJSON | C | 轻量，适合 C 项目和 MCU |
| nlohmann/json | C++ | 写法直观，适合 C++ 项目 |
| RapidJSON | C++ | 性能强，写法相对复杂 |

## 重点

> **C 项目常用 cJSON，C++ 项目常用 nlohmann/json。**

---

# 11. `json.hpp` 是什么

`json.hpp` 通常指 **nlohmann/json** 的单头文件版本。

使用方式：

```cpp
#include "json.hpp"
```

然后：

```cpp
using json = nlohmann::json;
```

或者：

```cpp
using nljson = nlohmann::json;
```

## 重点

> **`json.hpp` 不是普通声明头文件，它是 header-only library。**  
> 也就是：声明和实现都在这个头文件里。

---

# 12. `.hpp` 和 `.h` 的区别

从编译器角度看，`.hpp` 和 `.h` 都可以被 `#include`。

区别主要是命名习惯：

| 后缀 | 常见含义 |
|---|---|
| `.h` | C 或 C++ 头文件 |
| `.hpp` | C++ 头文件 |
| `.hh` / `.hxx` | C++ 头文件 |

## 重点

> **`.hpp` 通常表示：这个头文件里用了 C++ 特性。**

---

# 13. 为什么 nlohmann/json 不需要链接库

普通库通常是：

```text
xxx.h    // 声明
xxx.cpp  // 实现
libxxx.a / libxxx.so  // 编译后的库
```

使用时需要：

```cpp
#include "xxx.h"
```

编译时还要链接库。

但是 nlohmann/json 是：

```text
json.hpp  // 声明 + 实现都在里面
```

所以只需要：

```cpp
#include "json.hpp"
```

## 编译示例

```bash
g++ main.cpp -std=c++11 -o app
```

如果 `json.hpp` 在 include 文件夹：

```bash
g++ main.cpp -std=c++11 -I./include -o app
```

## 重点

> **不是“没有库”，而是这个库的实现已经在头文件里了。**

---

# 14. `nlohmann::json` 是什么

`nlohmann::json` 是 nlohmann/json 库提供的 C++ 类型。

```cpp
nlohmann::json j;
```

它可以表示任意 JSON 值：

```cpp
nlohmann::json a = 220.5;                   // number
nlohmann::json b = "DTU_001";               // string
nlohmann::json c = true;                    // boolean
nlohmann::json d = nullptr;                 // null
nlohmann::json e = {"wifi", "ethernet"};    // array
```

也可以表示对象：

```cpp
nlohmann::json payload;
payload["deviceId"] = "DTU_001";
payload["voltage"] = 220.5;
payload["online"] = true;
```

输出 JSON：

```json
{
  "deviceId": "DTU_001",
  "voltage": 220.5,
  "online": true
}
```

## 重点

> **`nlohmann::json` 是 C++ 对象，但它内部保存的是 JSON 数据。**

---

# 15. `using nljson = nlohmann::json;` 是什么

这是一种类型别名。

```cpp
using nljson = nlohmann::json;
```

意思是：

```cpp
nljson
```

等价于：

```cpp
nlohmann::json
```

所以：

```cpp
nljson config;
```

等价于：

```cpp
nlohmann::json config;
```

## 重点

> **`nljson` 不是库自带的新类型，只是你给 `nlohmann::json` 起的短名字。**

---

# 16. 读取 JSON 文件

假设有文件：

```text
gateway_config.json
```

C++ 读取方式：

```cpp
#include <iostream>
#include <fstream>
#include "json.hpp"

using json = nlohmann::json;

int main()
{
    std::ifstream file("gateway_config.json");

    if (!file.is_open()) {
        std::cerr << "打开配置文件失败" << std::endl;
        return 1;
    }

    json config;
    file >> config;

    std::string name = config["gateway"]["name"];
    std::string mode = config["network"]["mode"];
    int interval = config["publish"]["interval_ms"];
    double voltage = config["mock"]["voltage_base"];

    std::cout << name << std::endl;
    std::cout << mode << std::endl;
    std::cout << interval << std::endl;
    std::cout << voltage << std::endl;

    return 0;
}
```

## 重点

> **`file >> config;` 的意思是：从文件流读取 JSON 文本，并解析成 `json` 对象。**

---

# 17. `std::ifstream file` 是什么

```cpp
std::ifstream file("gateway_config.json");
```

含义：

> 创建一个输入文件流对象，变量名叫 `file`，打开 `gateway_config.json`。

需要头文件：

```cpp
#include <fstream>
```

常见文件流：

| 类型 | 作用 |
|---|---|
| `std::ifstream` | 读取文件 |
| `std::ofstream` | 写入文件 |
| `std::fstream` | 读写文件 |

## 重点

> **`file` 只是变量名，不是关键字，可以改成 `config_file`、`input` 等。**

---

# 18. `payload["deviceId"] = "DTU_001";` 是什么

```cpp
nlohmann::json payload;

payload["deviceId"] = "DTU_001";
payload["voltage"] = 220.5;
payload["online"] = true;
```

结果：

```json
{
  "deviceId": "DTU_001",
  "voltage": 220.5,
  "online": true
}
```

含义：

```cpp
payload["deviceId"]
```

表示访问 JSON 对象中 key 为 `deviceId` 的字段。

## 重点

> **`payload["deviceId"] = "DTU_001";` 可以直接写字符串。**  
> nlohmann/json 会自动把 C++ 字符串转换成 JSON string。

---

# 19. `std::map` 和 `nlohmann::json` 的区别

`std::map` 是 C++ 标准库里的键值对容器。

```cpp
std::map<std::string, int> m;
m["port"] = 11883;
```

它适合固定 value 类型的键值数据。

但 JSON 字段往往有多种类型：

```json
{
  "deviceId": "DTU_001",
  "modbus_addr": 1,
  "voltage": 220.5,
  "online": true
}
```

如果用：

```cpp
std::map<std::string, std::string>
```

会导致所有值都变成字符串，丢失类型。

而：

```cpp
nlohmann::json payload;
```

可以直接保存多种类型：

```cpp
payload["deviceId"] = "DTU_001";
payload["modbus_addr"] = 1;
payload["voltage"] = 220.5;
payload["online"] = true;
```

## 重点

> **`std::map` 是普通容器；`nlohmann::json` 是专门表示 JSON 的动态数据结构。**

---

# 20. `std::map<std::string, nlohmann::json>` 是什么

```cpp
std::map<std::string, nlohmann::json> values;
```

含义：

| 部分 | 含义 |
|---|---|
| `std::map` | C++ map 容器 |
| `std::string` | key 是字符串 |
| `nlohmann::json` | value 是 JSON 值 |
| `values` | 变量名 |

所以：

```cpp
values["voltage"] = 220.5;
values["current"] = 3.2;
values["online"] = true;
values["deviceId"] = "METER_001";
```

实际保存的是：

```text
"voltage"  -> json(220.5)
"current"  -> json(3.2)
"online"   -> json(true)
"deviceId" -> json("METER_001")
```

可以转换成 JSON：

```cpp
nlohmann::json j = values;
std::cout << j.dump(4) << std::endl;
```

输出：

```json
{
    "current": 3.2,
    "deviceId": "METER_001",
    "online": true,
    "voltage": 220.5
}
```

## 重点

> **`std::map<std::string, nlohmann::json>` 是“字符串 key -> 任意 JSON value”的映射。**

---

# 21. 为什么不能写 `std::map<std::string, auto>`

错误写法：

```cpp
std::map<std::string, auto> m;
```

原因：

> `std::map` 的 value 类型必须在编译期确定。

`auto` 不是“任意类型”，它只是让编译器根据初始化值推导出一个确定类型。

可以这样：

```cpp
auto x = 10;       // x 是 int
auto y = 220.5;    // y 是 double
```

但不能这样：

```cpp
std::map<std::string, auto> m;
```

因为编译器不知道 value 到底是：

```text
int?
double?
string?
bool?
json?
```

## 重点

> **`auto` 不是动态类型，也不是万能类型。**  
> 想让 value 支持多种 JSON 类型，应使用 `nlohmann::json`。

---

# 22. `values["voltage"] = 220.5;` 到底发生了什么

代码：

```cpp
std::map<std::string, nlohmann::json> values;

values["voltage"] = 220.5;
```

这里：

```cpp
values["voltage"]
```

的类型是：

```cpp
nlohmann::json&
```

也就是 map 中某个 `json` 值的引用。

执行过程：

1. 查找 key：`"voltage"`
2. 如果不存在，就创建一个默认的 `nlohmann::json` 值
3. 把普通 C++ double 值 `220.5` 赋给这个 JSON 值
4. 这个 JSON 值内部保存为 JSON number_float

等价理解：

```cpp
nlohmann::json temp = 220.5;
values["voltage"] = temp;
```

## 重点

> **`220.5` 本身是 C++ double。**  
> **赋给 `nlohmann::json` 后，变成 JSON number 值。**

---

# 23. JSON number 和 C++ number 的关系

```cpp
double x = 220.5;
```

这里 `x` 是 C++ double。

```cpp
nlohmann::json j = 220.5;
```

这里 `j` 是 C++ 的 `nlohmann::json` 对象，但它内部保存的 JSON 内容是：

```json
220.5
```

| 写法 | C++ 类型 | JSON 内容 |
|---|---|---|
| `double x = 220.5;` | `double` | 不是 JSON |
| `json j = 220.5;` | `nlohmann::json` | JSON number：`220.5` |
| `payload["voltage"] = 220.5;` | `payload["voltage"]` 是 `json&` | JSON number：`220.5` |

## 重点

> **数据意义上，JSON number 和 C++ 数字很像；但 C++ 类型系统里，JSON number 被包在 `nlohmann::json` 对象里。**

---

# 24. 一个 `json` 对象可以表示多种 JSON 类型

```cpp
nlohmann::json j1 = 220.5;
```

JSON 内容：

```json
220.5
```

---

```cpp
nlohmann::json j2 = "DTU_001";
```

JSON 内容：

```json
"DTU_001"
```

---

```cpp
nlohmann::json j3 = true;
```

JSON 内容：

```json
true
```

---

```cpp
nlohmann::json j4 = {"ethernet", "wifi", "cellular"};
```

JSON 内容：

```json
["ethernet", "wifi", "cellular"]
```

---

```cpp
nlohmann::json j5;
j5["voltage"] = 220.5;
```

JSON 内容：

```json
{
  "voltage": 220.5
}
```

## 重点

> **`nlohmann::json` 是一个 C++ 类型，但它内部可以装任意合法 JSON 值。**

---

# 25. 构造数组

## 25.1 直接构造字符串数组

```cpp
nlohmann::json priority = {"ethernet", "wifi", "cellular"};
```

结果：

```json
[
  "ethernet",
  "wifi",
  "cellular"
]
```

## 25.2 明确创建数组

```cpp
nlohmann::json arr = nlohmann::json::array();

arr.push_back("ethernet");
arr.push_back("wifi");
arr.push_back("cellular");
```

## 25.3 对象字段里放数组

```cpp
nlohmann::json network;
network["priority"] = {"ethernet", "wifi", "cellular"};
```

结果：

```json
{
  "priority": ["ethernet", "wifi", "cellular"]
}
```

## 重点

> **数组字段可以直接赋 initializer_list，例如 `payload["tags"] = {"root", "meter"};`。**

---

# 26. 构造对象数组

```cpp
nlohmann::json devices = nlohmann::json::array();

// 第一个设备
devices.push_back({
    {"device_id", "METER_001"},
    {"product_id", "single_phase_meter"},
    {"name", "METER_001"},
    {"type", "single_phase_meter"}
});

// 第二个设备
devices.push_back({
    {"device_id", "ENV_001"},
    {"product_id", "env_sensor"},
    {"name", "ENV_001"},
    {"type", "env_sensor"}
});
```

输出：

```json
[
  {
    "device_id": "METER_001",
    "product_id": "single_phase_meter",
    "name": "METER_001",
    "type": "single_phase_meter"
  },
  {
    "device_id": "ENV_001",
    "product_id": "env_sensor",
    "name": "ENV_001",
    "type": "env_sensor"
  }
]
```

---

# 27. 构造 MQTT 上报 payload 示例

```cpp
#include <iostream>
#include "json.hpp"

using json = nlohmann::json;

int main()
{
    json payload;

    payload["deviceId"] = "METER_001";
    payload["ts"] = 1710000000000;

    payload["values"]["voltage"] = 220.5;
    payload["values"]["current"] = 3.2;
    payload["values"]["power"] = 704.0;
    payload["values"]["frequency"] = 50.0;

    payload["tags"] = {"meter", "single_phase", "wireless"};

    std::cout << payload.dump(4) << std::endl;

    return 0;
}
```

输出：

```json
{
    "deviceId": "METER_001",
    "tags": [
        "meter",
        "single_phase",
        "wireless"
    ],
    "ts": 1710000000000,
    "values": {
        "current": 3.2,
        "frequency": 50.0,
        "power": 704.0,
        "voltage": 220.5
    }
}
```

---

# 28. 安全读取字段

直接读取：

```cpp
std::string ssid = config["network"]["wifi"]["ssid"];
```

如果字段不存在，可能出问题。

更安全：

```cpp
std::string ssid = config["network"]["wifi"].value("ssid", "");
int interval = config["publish"].value("interval_ms", 5000);
double voltage = config["mock"].value("voltage_base", 220.0);
```

含义：

> 有这个字段就读取，没有就使用默认值。

## 重点

> **读取配置文件时，推荐使用 `value("key", default_value)`。**

---

# 29. 常见错误总结

## 29.1 JSON 语法错误

| 错误 | 示例 |
|---|---|
| 少逗号 | `"a": 1 "b": 2` |
| 多逗号 | `{ "a": 1, }` |
| 单引号 | `{ 'a': 1 }` |
| key 没引号 | `{ a: 1 }` |
| 写注释 | `{ "a": 1 // 注释 }` |

---

## 29.2 C++ 使用错误

错误：

```cpp
std::map<std::string, auto> m;
```

原因：`auto` 不能作为 map 的 value 类型。

---

错误：

```cpp
std:string name;
```

正确：

```cpp
std::string name;
```

---

错误：

```cpp
payload[deviceId] = "DTU_001";
```

正确：

```cpp
payload["deviceId"] = "DTU_001";
```

---

# 30. 复习速记

## JSON 基础

- `{}` 是对象
- `[]` 是数组
- `"key": value` 是键值对
- 字符串必须双引号
- 数字不用引号
- 布尔值是小写 `true` / `false`
- 空值是 `null`
- 最后一项后面不能加逗号
- JSON 标准不支持注释

## C++ nlohmann/json

- `json.hpp` 是 nlohmann/json 的单头文件库
- `nlohmann::json` 是 C++ 类型
- `using json = nlohmann::json;` 是起别名
- `json j = 220.5;` 表示 JSON number
- `json j = {"wifi", "ethernet"};` 表示 JSON array
- `j["key"] = value;` 表示 JSON object 字段赋值
- `dump(4)` 可以格式化输出 JSON 字符串
- `file >> config;` 可以从文件读取并解析 JSON

## map 对比

- `std::map<std::string, int>`：value 只能是 int
- `std::map<std::string, std::string>`：value 只能是 string
- `std::map<std::string, nlohmann::json>`：value 可以是任意 JSON 值
- 但多数情况下，直接用 `nlohmann::json payload;` 更方便

---

# 31. 最终理解

## 一句话理解 JSON

> **JSON 是一种用 `{}`、`[]`、`"key": value` 表示结构化数据的文本格式。**

## 一句话理解 nlohmann::json

> **`nlohmann::json` 是 C++ 中用来表示 JSON 数据的万能容器。**

## 一句话理解 `values["voltage"] = 220.5;`

> **`220.5` 是普通 C++ double，赋给 `nlohmann::json` 后，被保存成 JSON number 值。**

## 一句话理解 `json.hpp`

> **`json.hpp` 是一个 header-only JSON 库，include 后就能直接使用 `nlohmann::json`。**

---

# 32. 推荐记忆模型

把 `nlohmann::json` 想成一个可以变形的盒子：

```cpp
json j = 220.5;
```

盒子里装数字。

```cpp
json j = "DTU_001";
```

盒子里装字符串。

```cpp
json j = {"wifi", "ethernet"};
```

盒子里装数组。

```cpp
json j;
j["voltage"] = 220.5;
```

盒子里装对象。

## 重点

> **C++ 类型始终是 `nlohmann::json`，但 JSON 内容可以是 number、string、bool、array、object、null。**
