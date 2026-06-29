# SLE 参数设置表

| 参数名 | client 默认值 | server 建议值 | 来源 | 是否可配置 | 说明 |
| --- | --- | --- | --- | --- | --- |
| `max_connections` | `8` | 至少支持 1 个 client 连接，按 server 能力配置 | 网关规划 / one_to_many 上限 | 是 | 第一版连接表固定 8 槽。 |
| `scan_interval` | `100` | 无强制要求 | 已跑通 WS73 `sle_uuid_client` | 是 | 扫描间隔。 |
| `scan_window` | `100` | 无强制要求 | 已跑通 WS73 `sle_uuid_client` | 是 | 扫描窗口。 |
| `scan_phy` | `SLE_SEEK_PHY_1M` | 广播 PHY 对齐 1M | 已跑通 WS73 `sle_uuid_client` | 暂否 | 第一版固定 1M，减少变量。 |
| `conn_interval` | `0x14` | 可兼容 `0x14`，也可参考 `0x64` | 已跑通 WS73 `sle_uuid_client` | 是 | `sle_one_to_many` 的 `0x64` 是 WS63 样例参数，不作为 WS73 Linux 默认值。 |
| `supervision_timeout` | `0x1f4` | `0x1f4` | 已跑通 WS73 `sle_uuid_client` / server sample | 是 | 链路超时，单位见 SDK。 |
| `mtu` | `1024` | server 支持 1024 或至少不拒绝 exchange | 当前 root 05/06 大包测试 | 是 | 保持文本/ASCII-hex 解析方式不变，只放宽单包长度。 |
| `mac_prefix` | `0x0200` | 当前 root 广播 MAC 前两字节使用 `02:00` | 网关扫描 root 识别规则 | 是 | 先按 MAC 前两字节过滤，再解析广播 LTV 厂商字段识别 `ST/DT` root。 |
| `data_property_uuid` | `0xFDF1` | Dtu `DTU_SLE_PROPERTY_UUID=0xFDF1` | 新增 Dtu SLE transport | 是 | server 中用于 write/notify 的属性 UUID。 |
| `auto_select_data_property` | `true` | Dtu 只有一个业务 property 时可开启 | 当前一对多 DTU 测试策略 | 是 | 如果 UUID 未匹配，但属性同时支持 notify/indicate 和 write，也会自动认作数据通道。 |
| `fallback_name_filter_enabled` | `false` | 不要求 | 调试兜底 | 是 | 仅调试旧样例时打开。 |
| `fallback_target_name` | `DTU_N01` | Dtu 默认设备名 | Dtu Kconfig | 是 | 只在 fallback 打开时使用。 |
| `enable_mcs` | `false` | 不要求 | 实测稳定性 | 是 | 第一版不主动 `sle_set_mcs`。 |
| `enable_phy_update` | `false` | 不要求 | 实测稳定性 | 是 | 第一版不主动 `sle_set_phy_param`。 |
| `enable_large_data_len` | `false` | 不要求 | 实测稳定性 | 是 | 第一版不主动 `sle_set_data_len`。 |
| `continue_scan_when_full` | `false` | 无强制要求 | 多连接架构约束 | 是 | 满 8 连接后默认停止发起新连接。 |

## Server 端对齐要点

- 多个 server 必须使用不同 SLE 地址，当前默认要求广播 MAC 前两字节为 `02:00`。
- root 广播必须包含 LTV 厂商字段：`0B FF 53 54/44 54 01 role node_id_lo node_id_hi free_slots root_lo root_hi depth`。只有 magic 为 `ST` 或 `DT` 且 `role=1(ROOT)` 的广播会进入候选连接。
- Dtu 当前 service UUID 为 `0xFDF0`，数据 property UUID 为 `0xFDF1`。
- Dtu 当前同一个 `0xFDF1` property 同时支持 read/write/notify，因此 client 会把同一个 handle 同时缓存为 notify/write handle。
- 如果 server 使用 WS63 样例 `conn_interval=0x64`，client 仍以 WS73 Linux 稳定默认 `0x14` 发起；如发现兼容性问题，再通过配置调整。
