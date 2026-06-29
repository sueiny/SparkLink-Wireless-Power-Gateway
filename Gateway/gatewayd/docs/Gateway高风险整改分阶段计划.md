# Gateway 高风险整改分阶段计划

> Status: Historical remediation plan.
> Authority: 仅用于理解整改阶段和历史验收思路，不作为当前实现或测试权威。
> Superseded by: [ST帧对接规定.md](ST帧对接规定.md), [gateway_config配置说明.md](gateway_config配置说明.md), [gatewayd与sle_data_app通信机制.md](gatewayd与sle_data_app通信机制.md).
> Last verified against: not fully revalidated; retained as historical reference on 2026-06-28.

## 背景

本轮整改同时覆盖 ST DATA 协议、root_report 严格名单、下行 ST 封装、`auto` 抢占式网络选择、路由/DNS/DHCP 对齐和测试脚本。为避免一次性大改后难以定位问题，按阶段推进，每阶段都保持可构建、可回放验证。

## 阶段 1：DATA 协议改为纯 Modbus RTU

目标：
- `0x02 DATA` 的 payload 完全等于 `modbus_rtu`。
- 不再在 DATA payload 中携带 `modbus_type` 和 `modbus_len`。
- `gatewayd` 通过 ST 头 `src_node_id` 查当前 `0x06` 映射，再从 JSON inventory 读取设备 `modbus_type/modbus_addr/type` 元数据。

实现：
- 修改 `SleDataPayload`，只保留 `modbus_rtu/modbus_len`。
- 修改 `parseSleDataPayload()`，把完整 payload 当作 RTU。
- 修改上行 mock、测试帧生成器、串口回放脚本，生成纯 RTU payload。
- 修改下行 builder，payload 直接写完整 RTU。

验收：
- 有效 `05 + 06 + DATA` 能解析并上报外接设备 telemetry。
- DATA payload 前两字节不再被解释为类型/长度。

## 阶段 2：root_report 严格名单与动态映射

目标：
- 正式 `root_report` 下，DATA 必须来自 JSON DTU 总名单内的 `src_node_id`。
- DATA 必须来自当前 `0x05` 在线 DTU。
- DATA 必须能通过当前 `0x06` 唯一映射到一个外接设备。
- 不再自动生成 `DTU_xxx`，不再把未知 DATA 当 DTU 遥测上报。

规则：
- 未知 DTU：丢弃 DATA。
- DTU 不在当前 05 在线快照：丢弃 DATA。
- 没有 06 映射：丢弃 DATA。
- 一个 DTU 映射多个外设：纯 RTU 无法区分，丢弃 DATA 并打印歧义日志。

验收：
- 未知 DTU DATA 不产生 telemetry。
- 未收到 06 前 DATA 不产生外设 telemetry。
- 06 引用未知 DTU 或未知外设继续拒绝。

## 阶段 3：下行 ST DATA 按目标 DTU 封装

目标：
- root 固件负责树内自动转发，gatewayd 只按协议写完整 ST DATA。
- IPC meta 的 root 仍用于选择实际 root 连接。
- ST 头 `dst_node_id` 写目标外设当前挂载的 DTU，而不是 root。
- ST payload 是纯 Modbus RTU，root 不需要理解业务类型。

验收：
- 下行 `set_relay` 生成的 ST 帧 `dst_node_id=target_dtu_id`。
- 下行 payload 直接以 Modbus RTU 起始，例如 `01 05 ... CRC`，无 `modbus_type/modbus_len` 前缀。

## 阶段 4：`auto` 抢占与网络对齐

目标：
- `auto` 模式固定按 `ethernet > wifi > cellular` 抢占。
- 高优先级恢复可真正上云时，在下一检查周期抢占。
- 候选探测不能破坏当前可用出口。
- 选中接口后，默认路由、DNS、MQTT 预期出口必须全部对齐。

实现：
- 候选探测阶段只确保候选默认路由存在，不删除当前默认路由。
- 候选探测用 `SO_BINDTODEVICE` 走目标接口做云端口连通测试。
- 候选通过后才执行严格对齐：只保留选中接口默认路由，并重写 `/etc/resolv.conf` 为选中接口 DNS。
- 如果 DHCP lease 没有 gateway，但接口有直连 IPv4 网段，则按当前板端规则推断网关为该网段 `.1`，用于 eth1 USB/RNDIS 场景。

指定模式边界：
- `network.mode=ethernet|wifi|cellular` 只检查指定 provider，不探测其它 provider，不抢占。
- 指定模式仍会修复当前接口 route/DNS/DHCP 对齐。

验收：
- 当前为 wlan0 时插入 eth1，eth1 真能连云则抢占 eth1。
- eth1 只有 IP 但无法连云时不抢占 wlan0。
- 当前选中 wlan0 时，不允许默认路由/DNS 漂到 eth1/eth2。

## 阶段 5：自启网络脚本指定模式解析修复

目标：
- `/etc/init.d/S42gateway-network-policy` 只读取 `network.mode`。
- 不再误把 `credential_mode` 等其它 `"mode"` 字段当网络模式。

验收：
- 配置 `network.mode=wifi` 时脚本只尝试 wlan0。
- 配置 `network.mode=ethernet` 时脚本只尝试 eth1。
- 配置 `network.mode=auto` 时按 eth1/wlan0/ppp0 顺序尝试。

## 阶段 6：文档与模拟 socket 测试

目标：
- 更新 `ST帧对接规定.md`、`Root_ST_05_06_DATA对接说明.md`、`上下行测试包格式与用例.md` 中 DATA 格式。
- 更新测试脚本，保证生成和解析的 DATA 都是纯 RTU。
- 用 socket 脚本完整回放 `05/06/DATA`，覆盖正常、少设备、多设备、未知名单、缺 06、下行 ST 解析。

最低测试：
- `bash app/Gateway/.claude/skills/run-gateway/driver.sh build-gw`
- `bash app/Gateway/.claude/skills/run-gateway/driver.sh build-sle`
- host 侧生成纯 RTU DATA 干跑。
- socket 回放：有效 05/06/DATA 有 telemetry；未知 DTU、缺 06、歧义映射无 telemetry。
