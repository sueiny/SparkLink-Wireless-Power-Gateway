# SLE Gateway 通信协议规范

> Status: Historical protocol reference.
> Authority: 仅用于理解旧版 SLE Gateway 协议设计背景；当前 gateway-root ST 对接不以本文为准。
> Superseded by: [ST帧对接规定.md](ST帧对接规定.md), [gatewayd与sle_data_app通信机制.md](gatewayd与sle_data_app通信机制.md).
> Last verified against: not fully revalidated; archived and compressed on 2026-06-28.

本文原先描述的是旧版 “Gateway 最多连接 3 个固定 root、Root 1/2/3、TOPO_SUMMARY 路由表、DATA payload 携带设备类型/长度” 方案。当前实现已经收敛为：

- gateway-root 主链路只使用 `DATA(0x02)`、`DTU_NETWORK_TOPOLOGY(0x05)`、`EXTERNAL_DEVICE_MAP(0x06)`。
- `DATA(0x02)` payload 完全等于 Modbus RTU，不再携带 `device_type/modbus_type/modbus_len`。
- `0x05/0x06/DATA` 使用 `gateway_config.json` 中 DTU inventory 的 `node_id`，不是 `device_id` 后缀。
- 当前配置是 69 个 DTU 和 9 个外设；`DTU_001..DTU_009` 的 `node_id` 为 `101..109`，`DTU_010..DTU_069` 的 `node_id` 为 `10..69`。
- 下行 `set_relay` 已由 `gatewayd` 构造完整 raw ST，经 `sle_data_app` raw ST bridge 写入 SLE；非 raw ST method 仍不作为真实下行验收依据。

## 当前应阅读

| 主题 | 文档 |
| --- | --- |
| ST 帧字段、payload、拒绝规则 | [ST帧对接规定.md](ST帧对接规定.md) |
| `gatewayd` 与 `sle_data_app` IPC、raw ST bridge | [gatewayd与sle_data_app通信机制.md](gatewayd与sle_data_app通信机制.md) |
| `gateway_config.json` inventory、`root_report` 边界 | [gateway_config配置说明.md](gateway_config配置说明.md) |
| root 固件/PC 脚本组帧注意事项 | [Root_ST_05_06_DATA对接说明.md](Root_ST_05_06_DATA对接说明.md) |

## 已过时假设

以下内容如在旧笔记或历史测试记录中出现，只能作为历史背景：

- `HEARTBEAT(0x01)`、`TOPO_SUMMARY(0x03)`、`DEPTH_UPDATE(0x04)` 进入 gateway-root 主链路。
- root 固定为 `1/2/3` 或当前验收使用 root `1/12`。
- `DTU_001` 等同于 ST `node_id=1`。
- DATA payload 形如 `device_type + modbus_len + modbus_rtu`。
- `sle_data_app` 负责解析业务命令或构造 Modbus。
- `relay set (mock)` 作为当前命令成功回包。

保留本页的目的只是避免旧链接失效，并提醒读者不要继续复制旧协议表。
