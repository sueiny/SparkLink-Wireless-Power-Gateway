# gatewayd docs

> Status: Current navigation entry.
> Authority: Use this file to decide which `gatewayd/docs` document to read first.
> Superseded by: None.
> Last verified against: `gatewayd/config/gateway_config.json` and current source tree on 2026-06-28.

这个目录保存 `gatewayd` 模块自身的工程文档。运行期协议、模块边界、配置、物模型、IPC、MQTT、SQLite、命令链路和板端实测细节，优先维护在本目录。

`app/Gateway/docs` 只保留复盘、学习和索引入口；需要改工程细节时优先改本目录。Obsidian 笔记流可使用同名双链，例如 `[[ST帧对接规定]]`。

## 当前权威文档

| 主题 | 主入口 | 说明 |
| --- | --- | --- |
| gateway-root ST 协议 | [ST帧对接规定.md](ST帧对接规定.md) | 唯一 ST 协议权威页；`0x02/0x05/0x06`、帧头、payload、拒绝规则以此为准。 |
| 配置与 inventory | [gateway_config配置说明.md](gateway_config配置说明.md) | `gateway_config.json` 字段、`root_report` 边界、69 个 DTU 和 9 个外设的当前配置约束。 |
| gatewayd 与 sle_data_app IPC | [gatewayd与sle_data_app通信机制.md](gatewayd与sle_data_app通信机制.md) | 数据上行、命令下行、abstract socket、raw ST bridge 和调试点。 |
| ThingsKit 物模型 | [things_model_v2.md](things_model_v2.md) | 当前 ThingsKit/DTU_001..DTU_069 物模型说明；历史模型见 [gateway_device_model.md](gateway_device_model.md)。 |
| 网络管理 | [网络管理模块实现细节.md](网络管理模块实现细节.md) | NetManager、Provider、DHCP、DNS、Netlink 路由和 MQTT 连接关系。 |
| 板端实测 | [板端实测使用流程.md](板端实测使用流程.md) | 板端构建、推送、运行、日志、网络、SLE IPC 和 root 联调流程。 |
| 板端手动操作 | [板端手动操作指令集合.md](板端手动操作指令集合.md) | 高频 ADB、driver.sh、日志查看和上下行排查命令集合。 |

## 实现细节

| 文档 | 当前用途 |
| --- | --- |
| [gatewayd框架理解.md](gatewayd框架理解.md) | `gatewayd` 主流程、线程、IPC、MQTT、SQLite、命令链路的框架笔记。 |
| [云到DTU上下行链路与包格式.md](云到DTU上下行链路与包格式.md) | 云端、gatewayd、IPC、`sle_data_app`、DTU root 的上下行链路说明；协议字段跳转到 ST/IPC 权威页。 |
| [离线规则引擎实现细节.md](离线规则引擎实现细节.md) | 离线规则、阈值配置、状态机、事件和本地控制链路。 |
| [本地AI实现细节.md](本地AI实现细节.md) | 离线 AI 的模型格式、配置、特征、事件输出和验证方式。 |
| [Modbus寄存器仿真规格.md](Modbus寄存器仿真规格.md) | Modbus 设备响应格式和寄存器解释，供 `codec/modbus_parser.cpp` 对照。 |
| [DTU_SLE_TREE数据帧格式.md](DTU_SLE_TREE数据帧格式.md) | DTU SLE Tree 内部协议参考；不是 gateway-root ST 主链路权威。 |

## 测试与对接记录

| 文档 | 当前用途 |
| --- | --- |
| [Root_ST_05_06_DATA对接说明.md](Root_ST_05_06_DATA对接说明.md) | 给 root 固件和 PC 模拟脚本实现者使用的组帧、发送顺序、拒绝规则和验收日志；协议字段以 [ST帧对接规定.md](ST帧对接规定.md) 为准。 |
| [上下行测试包格式与用例.md](上下行测试包格式与用例.md) | 真实 SLE 联调测试包、COM/ADB 命令和历史/当前验收结论；旧 heartbeat/static_json/31 DTU/11 外设记录只作历史压力参考。 |

## 历史参考

| 文档 | 当前用途 |
| --- | --- |
| [sle_gateway_protocol.md](sle_gateway_protocol.md) | 旧版 SLE Gateway 协议设计参考，已被 [ST帧对接规定.md](ST帧对接规定.md) 收敛。 |
| [gateway_device_model.md](gateway_device_model.md) | 旧版物模型设计和迁移参考，当前物模型看 [things_model_v2.md](things_model_v2.md)。 |
| [ST动态拓扑上报帧05_06设计与改造计划.md](ST动态拓扑上报帧05_06设计与改造计划.md) | 05/06 动态拓扑改造计划和历史设计背景；最终协议看 [ST帧对接规定.md](ST帧对接规定.md)。 |
| [Gateway高风险整改分阶段计划.md](Gateway高风险整改分阶段计划.md) | 高风险整改阶段计划和历史验收思路，不作为当前验收清单。 |

## 维护规则

- 运行期协议、字段、socket、topic、解析规则变更时，优先更新本目录的权威文档。
- ST 字段和 gateway-root 帧集合只维护在 [ST帧对接规定.md](ST帧对接规定.md)，其他文档保留实现说明、测试命令或历史背景。
- `gateway_config.json` 当前配置事实以源码和配置文件为准：`topology.expected_dtu_count=69`、`topology.expected_external_device_count=9`、`devices[]` 内 DTU inventory 69 个、外设 inventory 9 个。
- 复盘和学习版本只在 `app/Gateway/docs/20_技术沉淀` 保留摘要和跳转，不再复制整篇工程细节。
- 如果改动同时影响 `sle_data_app`，同步更新 `app/Gateway/sle_data_app/docs` 或相关测试脚本说明。
