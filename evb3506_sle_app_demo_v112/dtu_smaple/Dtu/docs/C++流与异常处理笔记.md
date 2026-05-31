# C++ IO 流与异常处理笔记

> 📅 整理日期：2026-05-17
> 📖 内容：流家族、重载、构造析构、异常处理、缓冲区、信号机制

---

## 一、流家族总览

```
stream
  ├── 输入流（读取）
  │     ├── istream
  │     │     ├── cin        → 终端输入
  │     │     └── ifstream   → 文件输入
  │     └── istringstream    → 字符串输入
  │
  └── 输出流（写入）
        ├── ostream
        │     ├── cout       → 终端输出 (console output)
        │     ├── cerr       → 终端错误 (console error)
        │     └── ofstream   → 文件输出
        └── ostringstream    → 字符串输出
```

> **💡 命名规则**
> - `c` = console（控制台）
> - `i` = input（输入）
> - `o` = output（输出）
> - `f` = file（文件）
> - `stringstream` = string stream（字符串流）

---

## 二、核心区别

| 类 | 目标 | `.str()` 方法 | 用途 |
|------|--------|-----------|------|
| `ofstream` | 文件 | ❌ 没有 | 持久化存储 |
| `ostringstream` | 字符串 | ✅ 有 | 临时拼接字符串 |
| `ifstream` | 文件 | - | 读取文件 |
| `istringstream` | 字符串 | - | 从字符串读取 |

> **⚠️ 易错点**
> - `ofstream` 没有 `.str()` 方法
> - 要获取字符串内容，必须用 `ostringstream`

```cpp
// ostringstream：可以直接获取字符串
std::ostringstream ss;
ss << "hello" << 123;
std::string content = ss.str();  // ✓ 可以

// ofstream：不能获取字符串
std::ofstream file("test.txt");
file << "hello" << 123;
// file.str();  // ✗ 编译错误！
```

---

## 三、`<<` 和 `>>` 重载

### 3.1 什么是重载

> **💡 重载** = 同一个符号/函数名，有不同的功能

```cpp
// << 原本是位运算
int a = 1 << 3;  // 左移 3 位 = 8

// C++ 重载为输出
std::cout << "hello";  // 输出字符串
std::cout << 123;      // 输出整数
```

### 3.2 `<<` 塞进去（输出）

```cpp
// 输出到终端
std::cout << 1.5 << 2 << "yes" << std::endl;

// 输出到文件
std::ofstream file("test.txt");
file << 1.5 << 2 << "yes" << std::endl;

// 输出到字符串
std::ostringstream ss;
ss << 1.5 << 2 << "yes" << std::endl;
```

### 3.3 `>>` 取出来（输入）

```cpp
int age;
double score;
std::string name;

// 从终端读取
std::cin >> age >> score >> name;

// 从文件读取
std::ifstream in("data.txt");
in >> age >> score >> name;

// 从字符串读取
std::string data = "25 95.5 张三";
std::istringstream ss(data);
ss >> age >> score >> name;
```

> **💡 `>>` 读取规则**
> - `int`：读取数字，遇到非数字停止
> - `double`：读取数字和小数点，遇到其他停止
> - `string`：读取连续字符，遇到空格/换行停止

---

## 四、构造函数与析构函数（RAII）

### 4.1 基本概念

```cpp
class MyClass {
public:
    MyClass() {
        std::cout << "构造" << std::endl;  // 创建对象时调用
    }
    ~MyClass() {
        std::cout << "析构" << std::endl;  // 销毁对象时调用
    }
};

{
    MyClass obj;  // 构造
}  // 离开作用域，自动析构
```

### 4.2 RAII 模式

> **✅ RAII = Resource Acquisition Is Initialization**
> 资源获取即初始化：构造获取资源，析构释放资源

```cpp
// C++：自动管理资源
{
    std::ifstream in("file.txt");  // 构造：自动打开
    // 使用 in...
}  // 析构：自动关闭文件

// C 语言：手动管理资源
FILE *fp = fopen("file.txt", "r");  // 手动打开
// 使用 fp...
fclose(fp);  // 手动关闭（容易忘记）
```

### 4.3 C vs C++

| 操作 | C 语言 | C++ |
|------|--------|-----|
| 打开文件 | `fopen()` | `ifstream in()` |
| 关闭文件 | `fclose()` | 自动析构 |
| 内存分配 | `malloc()` | `new` |
| 内存释放 | `free()` | `delete` / 自动析构 |

---

## 五、异常处理（try-catch）

### 5.1 基本语法

```cpp
try {
    // 可能出错的代码
    root = nlohmann::json::parse(text);
} catch (const std::exception &e) {
    // 捕获异常
    std::cout << e.what() << std::endl;  // 获取错误信息
}
```

### 5.2 throw 抛出异常

```cpp
// 抛出异常
throw std::runtime_error("出错了");

// 等价于
std::runtime_error err("出错了");
throw err;
```

### 5.3 异常类层次

```
std::exception          ← 基类
  ├── std::runtime_error    ← 运行时错误
  │     ├── std::invalid_argument  ← 参数无效
  │     ├── std::out_of_range      ← 越界
  │     └── std::overflow_error    ← 溢出
  └── std::logic_error      ← 逻辑错误
```

> **⚠️ 头文件**
> - `<stdexcept>`：标准异常类
> - `<exception>`：基类 `std::exception`
> - `nlohmann/json.hpp` 已包含这两个头文件

### 5.4 谁抛出的异常？

> **🔴 关键点**
> 你没有 `throw`，是 **nlohmann::json 库内部**抛出的异常

```cpp
// json 库内部（简化）
static json parse(const std::string &text) {
    if (格式错误) {
        throw std::runtime_error("parse error");  // 库抛出异常
    }
}

// 你的代码
root = nlohmann::json::parse(text);  // 如果格式错误，跳到 catch
```

---

## 六、缓冲区与 flush

### 6.1 什么是缓冲区

> **💡 缓冲区** = 8KB 的临时存储区，攒够了再写入目标

```
程序 → [缓冲区 8KB] → flush → 屏幕/文件
```

### 6.2 `\n` vs `endl`

```cpp
std::cout << "hello\n";       // 只换行，不刷新
std::cout << "hello" << endl; // 换行 + 立即刷新
```

| 操作 | 效果 | 性能 |
|------|------|------|
| `\n` | 写入缓冲区 | 快 |
| `endl` | 写入缓冲区 + flush | 慢 |

> **✅ 最佳实践**
> - 高频写入：用 `\n` + 手动 `flush`
> - 需要立即显示：用 `endl`

### 6.3 触发 flush 的时机

1. **缓冲区满**（8KB）→ 自动 flush
2. **遇到 `\n`**（stdout 行缓冲）→ 自动 flush
3. **调用 `flush()` 或 `endl`** → 手动 flush
4. **程序结束** → 析构函数自动 flush

---

## 七、stderr vs stdout

| 流 | 文件描述符 | 默认目标 | 用途 |
|------|--------|----------|------|
| `stdin` | 0 | 键盘 | 标准输入 |
| `stdout` | 1 | 终端 | 标准输出 |
| `stderr` | 2 | 终端 | 标准错误 |

### 重定向

```bash
# stdout 重定向到文件
./program > output.txt    # stderr 仍在终端

# stderr 重定向到文件
./program 2> error.txt    # stdout 仍在终端

# 都重定向
./program > all.txt 2>&1

# 丢弃所有输出
./program &>/dev/null
```

> **💡 为什么用 stderr**
> - 错误信息需要**立即可见**
> - 不受 stdout 重定向影响
> - 无缓冲，立即输出

---

## 八、信号机制（pending）

### 8.1 信号是什么

```cpp
// Ctrl+C → SIGINT (2)
// kill   → SIGTERM (15)

void handleSignal(int) {
    g_quit.store(true);  // 原子设置退出标志
}
```

### 8.2 pending 位图

> **💡 pending** = 待处理信号的标记位图，每个信号占 1 bit

```
信号编号:  1  2  3  4  5  6  ...  15  ...
pending:   0  1  0  0  0  0  ...  1   ...
                ↑                    ↑
             SIGINT收到           SIGTERM收到
             但还没处理            但还没处理
```

### 8.3 检测流程

```
Ctrl+C 按下
  → 键盘硬件中断
  → 内核中断处理程序
  → 内核标记 SIGINT pending
  → 进程返回用户态时检查 pending
  → 调用 handleSignal(2)
```

---

## 九、头文件速查

| 头文件 | 功能 | 示例 |
|--------|------|------|
| `<cerrno>` | 错误码 | `errno`, `strerror()` |
| `<cstring>` | C 字符串 | `strlen()`, `strcpy()` |
| `<fcntl.h>` | 文件控制 | `open()`, `O_RDONLY` |
| `<fstream>` | 文件流 | `ifstream`, `ofstream` |
| `<sstream>` | 字符串流 | `istringstream`, `ostringstream` |
| `<sys/stat.h>` | 文件状态 | `stat()`, `mkdir()` |
| `<sys/types.h>` | 系统类型 | `pid_t`, `ssize_t` |
| `<unistd.h>` | UNIX 函数 | `read()`, `write()`, `fork()` |

---

## 十、常见问题

### Q1: ofstream 能获取字符串吗？

**不能**。`ofstream` 没有 `.str()` 方法，只有 `ostringstream` 有。

```cpp
// 正确做法：用 ostringstream
std::ostringstream ss;
ss << "hello" << 123;
std::string content = ss.str();
```

### Q2: json 解析怎么知道出错了？

**不是你 throw 的，是 json 库内部 throw 的**。

```cpp
// json 库内部
if (格式错误) {
    throw std::runtime_error("parse error");
}

// 你的代码
try {
    root = nlohmann::json::parse(text);
} catch (const std::exception &e) {
    // 捕获库抛出的异常
}
```

### Q3: value() 和 object() 配合是什么？

```cpp
// 安全地取嵌套 JSON
auto gateway = root.value("gateway", nlohmann::json::object());
// 有 "gateway" → 返回实际值
// 没有 "gateway" → 返回 {}（空对象，不崩溃）
```

> **⚠️ 记住**
> - `object()` = `{}` 空对象
> - `array()` = `[]` 空数组
> - 用于提供安全的默认值

---

## 十一、速记口诀

```
流家族：
  cin/cout → 终端
  ifstream/ofstream → 文件
  istringstream/ostringstream → 字符串

重载：
  << 塞进去（输出）
  >> 取出来（输入）

RAII：
  构造获取资源
  析构释放资源

异常：
  try 尝试
  catch 捕获
  throw 抛出

缓冲区：
  \n → 慢但省资源
  endl → 快但耗资源
```
