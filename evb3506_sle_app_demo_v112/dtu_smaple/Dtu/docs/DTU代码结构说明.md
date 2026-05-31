# DTU 代码结构说明

当前 DTU 按“总控 + CONFIG + RUN + 外部通道”组织，避免把所有内部逻辑继续堆在 `service/` 下。

## 文件分层

| 路径 | 角色 | 主要职责 |
| --- | --- | --- |
| `dtu_main.c` | 启动层 | 创建 DTU 初始化任务 |
| `manager/dtu_service.c/.h` | 总控入口 | storage/board/transport 初始化、输入分流、统一配置协议回包出口 |
| `manager/dtu_service_internal.h` | manager 内部接口 | transport 查询、命令/通道名称、命令表查找、回包接口 |
| `config/dtu_config.c/.h` | CONFIG facade | 字节流入口、解析状态处理、命令分发入口；`.h` 同时收口配置协议共享类型和 config 内部声明 |
| `config/dtu_config_protocol.c` | CONFIG 协议算法 | AA55 parser、CRC、配置响应帧打包 |
| `config/dtu_config_commands.c` | CONFIG 命令层 | GET_MODE_STATUS、REBOOT、配置命令 handler、命令表 |
| `run/dtu_run.c/.h` | RUN 主线 | SLE / UART0 / UART1(485) 透明桥接，后续调用 `run/mesh/` |
| `run/mesh/` | RUN mesh 预留 | 后续组网协议实现目录 |
| `storage/dtu_storage.c/.h` | 存储中心 | 默认值、NV 读写、当前模式、运行配置缓存 |
| `transport/dtu_channel_uart.c` | UART 通道 | UART0/UART1 初始化、RX ring、DMA 发送、UART 任务 |
| `transport/dtu_channel_ble.c` | BLE 通道 | BLE GATT server、RX ring、notify 发送、BLE 任务 |
| `transport/dtu_channel_sle.c` | SLE 通道 | SLE server、RX ring、notify 发送、SLE 任务 |
| `transport/dtu_transport.h` | 通道接口 | transport if、通道对象、UART 子通道发送接口 |
| `board/dtu_board.c/.h` | 板级接口 | DIP 模式读取、LED 初始化与状态刷新 |
| `common/dtu_log.c/.h` | 统一日志 | `[DTU LOG]` 前缀、配置快照、读写配置、错误日志 |
| `common/dtu_build_config.h` | 可调配置总入口 | 引脚、线程、UART 默认值、命令字、状态码、容量限制 |
| `common/dtu_types.h` | 跨模块数据结构 | mode、transport、runtime cfg 等共享 enum/struct |

## 主链路

CONFIG 模式收到配置协议：

```text
transport
    -> dtu_service_on_bytes()
    -> manager/dtu_service.c 判断当前是 CONFIG
    -> config/dtu_config_on_bytes()
    -> config/dtu_config_protocol.c 的 AA55 parser / CRC
    -> dtu_config_dispatch()
    -> config/dtu_config_commands.c 的命令表
    -> dtu_service_reply()
    -> transport send()
```

RUN 模式透明桥接：

```text
UART0 / SLE / UART1(485)
    -> dtu_service_on_bytes() 或 dtu_service_on_uart485_bytes()
    -> manager/dtu_service.c 判断当前是 RUN
    -> run/dtu_run.c
    -> UART0 打印观察 / UART1(485) 发送 / SLE notify
```

配置保存和重启生效：

```text
config handler
    -> dtu_storage_runtime()
    -> dtu_storage_commit()
    -> NV_ID_DTU_CFG
    -> reboot 后 dtu_storage_load()
```

## 关键入口

| 函数 | 文件 | 作用 |
| --- | --- | --- |
| `dtu_service_init()` | `manager/dtu_service.c` | DTU 总初始化入口 |
| `dtu_service_on_bytes()` | `manager/dtu_service.c` | UART0/BLE/SLE 提交原始字节 |
| `dtu_service_on_uart485_bytes()` | `manager/dtu_service.c` | UART1/485 提交 RUN 模式回包 |
| `dtu_service_reply()` | `manager/dtu_service.c` | 配置协议统一回复出口 |
| `dtu_config_on_bytes()` | `config/dtu_config.c` | CONFIG 字节流解析入口 |
| `dtu_config_dispatch()` | `config/dtu_config.c` | CONFIG 命令表分发 |
| `dtu_run_on_sle()` | `run/dtu_run.c` | SLE 下发数据进入 RUN bridge |
| `dtu_run_on_uart0()` | `run/dtu_run.c` | UART0 调试输入进入 RUN bridge |
| `dtu_run_on_485()` | `run/dtu_run.c` | UART1/485 返回数据进入 RUN bridge |
| `dtu_storage_load()` | `storage/dtu_storage.c` | 从 NV 加载配置，并按 GPIO13 拨码决定当前模式 |
| `dtu_storage_commit()` | `storage/dtu_storage.c` | 将当前配置写入 NV |

## 生效规则

| 项 | 运行时修改 | `COMMIT` 后 | `REBOOT` 后 |
| --- | --- | --- | --- |
| `role` | 改内存 | 写入 NV | 重启后恢复 |
| `uart_cfg` | 改内存 | 写入 NV | 按新串口参数初始化 |
| `current_mode` | 由 GPIO13 拨码决定 | 不作为生效依据 | 重启时重新读取拨码 |
| `modbus / power / whitelist` | 改内存 | 写入 NV | 重启后恢复 |

GPIO13 高电平进入 CONFIG，低电平进入 RUN。`SET_UART_CFG` 不立即重配 UART1/485，必须 `COMMIT` 后重启才生效。

## 推荐阅读顺序

想改默认配置/线程/容量：先看 `common/dtu_build_config.h`。

想看 CONFIG 协议：先看 `config/dtu_config.c`。

想看 RUN 透明桥接/后续 mesh：先看 `run/dtu_run.c`，后续 mesh 放在 `run/mesh/`。

想看通道接入：先看 `transport/dtu_channel_uart.c`，再对照 BLE/SLE。

transport 文件末尾统一集中放 task、init/send 实现和 `g_dtu_xxx_transport` 接口表，找“谁启动、谁收数据、谁发数据”直接看文件底部。
