# gatewayd docs

这个目录保存 `gatewayd` 模块自身的工程文档。这里是源码旁边的主文档位置，适合记录运行期协议、模块边界、配置、物模型、IPC、MQTT、SQLite 和命令链路细节。

`app/Gateway/docs` 只保留复盘、学习和索引入口；需要改工程细节时优先改本目录。

## 当前文档

- `gatewayd框架理解.md`：`gatewayd` 主流程、线程、IPC、MQTT、SQLite、命令链路。
- `SLE数据源替换MockDataSource计划.md`：从 Mock 数据源切换到真实 SLE IPC 数据源的改造计划。
- `Modbus寄存器仿真规格.md`：当前 Modbus 设备响应格式和寄存器解释，供 `codec/modbus_parser.cpp` 对照。
- `sle_gateway_protocol.md`：SLE ST 帧协议说明。
- `gateway_device_model.md`：网关设备模型设计。
- `things_model_v2.md`：ThingsKit 物模型定义。

## 维护规则

- 运行期协议、字段、socket、topic、解析规则变更时，优先更新本目录。
- 复盘和学习版本只在 `app/Gateway/docs/20_技术沉淀` 保留摘要和跳转，不再复制整篇工程细节。
- 如果改动同时影响 `sle_data_app`，同步更新 `app/Gateway/sle_data_app/docs`。
