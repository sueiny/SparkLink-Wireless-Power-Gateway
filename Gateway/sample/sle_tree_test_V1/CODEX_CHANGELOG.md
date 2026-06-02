# Codex Major Change Log (sle_tree_test_V1)

这个文件用于记录 **重大改动**，方便你审查、回溯和跨会话移植。

## 记录规则

- 只记录会影响行为/拓扑/协议/容量/稳定性的改动。
- 每条记录包含：时间、动机、改动点、影响文件、验证方式。
- 小型日志文案调整、注释优化不单独记录。

---

## 2026-04-24

### 1) 角色容量上限按角色区分（root=8, relay=7）

- 动机：
  - 现场约束是 relay 需要预留 1 条父连接，child 不应占满 8。
- 改动点：
  - 引入 `sle_tree_max_children_for_role()`。
  - root 使用 8；relay 使用 `SLE_TREE_MAX_CHILDREN - 1`。
  - 广播 `free_slots` 与是否继续广播均改为按角色上限计算。
- 影响文件：
  - `inc/ST_test_internal.h`
  - `src/ST_test_route.c`
  - `src/ST_test_link.c`
- 验证：
  - 观察 relay 端 `child add conn=` 最大不超过 7（保留父链路）。

### 2) 父节点打分改为“层级优先”

- 动机：
  - root 重启后，因 `free_slots` 权重过高，节点容易继续挂到 depth=1 relay，收敛慢且不稳定。
- 改动点：
  - 调整 `sle_tree_candidate_score()` 权重，提升 depth 优先级，降低 free_slots/RSSI 权重。
  - 新增降层迁移阈值 `SLE_TREE_REPARENT_DEPTH_MARGIN`，当新父节点可降低层级时，迁移门槛更低。
  - 迁移日志增加 `margin/need`，便于现场判断为何迁移或不迁移。
- 影响文件：
  - `inc/ST_test_internal.h`
  - `src/ST_test_link.c`
- 验证：
  - 候选日志中在可连 root 时更稳定选择低 depth 父节点。

### 3) 扫描候选容量不足导致“看起来扫不全”

- 动机：
  - 现场可广播节点数大于 8 时，旧候选表会截断，后续候选静默丢弃。
- 改动点：
  - `SLE_TREE_MAX_CANDIDATES: 8 -> 24`。
  - `SLE_TREE_SCAN_COLLECT_WINDOW_MS: 2000 -> 3000`。
- 影响文件：
  - `inc/ST_test_internal.h`
- 验证：
  - 候选日志条目数明显增加，不再长期固定少量节点。

### 4) relay 重连后立即上报，缩短 root 重建拓扑窗口

- 动机：
  - 之前要等周期上报，root 重启后短时间内拓扑信息不完整。
- 改动点：
  - relay uplink handle ready 后，立即发送 heartbeat + topo summary。
- 影响文件：
  - `src/ST_test.c`
- 验证：
  - root 重启后更快看到路由/拓扑恢复。

### 5) 关键修复：uplink 误判成 child，导致 `DST=1 ERR=unreachable`

- 动机：
  - 日志出现“pick parent 成功并连接 root，但仍不可达”，并伴随父连接被记成 `child add conn=...`。
- 根因：
  - `sle_tree_addr_equal()` 以前比较了 `addr->type`；现场同一设备在不同回调中 `type` 可能不一致，导致 uplink 判断失败。
- 改动点：
  - 地址比较改为 **仅比较 MAC 字节**，忽略 type 字段。
- 影响文件：
  - `src/ST_test.c`
- 验证：
  - 连接 root 后不再把该链路误记为 child。
  - `DST=1 ERR=unreachable` 显著减少。

### 6) root 直连 child 连接即入拓扑（不再只等业务帧）

- 动机：
  - 出现“已 Connected，但 topo 暂未显示该节点”的短窗口。
- 改动点：
  - root 在 `sle_tree_alloc_child()` 成功后，立即 `sle_tree_root_touch_direct_child(...)`。
  - 在该 demo 中先按 relay 角色入拓扑，后续可被业务帧身份刷新。
- 影响文件：
  - `src/ST_test_route.c`
- 验证：
  - 新连入节点更快出现在 root topo 树中。

### 7) root 重启后“连接态但不可达 / topo 静默掉节点”诊断与修复

- 动机：
  - root 重启后，relay 可能日志显示已连接 root，但仍出现 `DST=1 ERR=unreachable`。
  - topo 图上一轮正常、下一轮少节点时，之前没有打印任何清理原因。
- 根因判断：
  - relay 侧父连接可能被误分类为 child，导致没有进入 uplink exchange/find property，`handle_ready` 不成立。
  - root topo 表独立老化，普通数据帧以前只刷新 route，不刷新 topo 活跃时间。
  - `ssapc_write_req()` 成功只代表请求发起成功，最终 write cfm 失败之前没有处理。
- 改动点：
  - uplink 判断增加 `node_id` 兜底匹配，并允许使用 `last_parent_node_id` 识别重连 root。
  - 连接回调新增 `conn classify ... as=uplink/child` 日志。
  - root 收到 child 帧后刷新对应 topo 活跃时间。
  - topo 超时/summary 剪枝/删除增加 `topo stale/prune/drop` 日志。
  - uplink write req/cfm 失败时打印日志并清理 uplink，触发重扫。
  - `DST=... ERR=unreachable` 前增加 `local send fail ...` 状态日志。
- 影响文件：
  - `inc/ST_test_internal.h`
  - `src/ST_test.c`
  - `src/ST_test_proto.c`
  - `src/ST_test_relay.c`
  - `src/ST_test_route.c`
- 验证：
  - 下一轮现场日志应能明确区分：误分类、handle 未 ready、write 失败、topo stale 或 summary prune。

### 8) root 重启后 uplink 已连接但未 ready 即断开的处理

- 动机：
  - 新日志显示 relay 能正确分类为 `as=uplink`，但 root 重启后链路在 `uplink handle ready` 之前断开，随后持续 `handle_ready=0`。
  - 这种失败以前没有进入父节点失败统计，下一轮仍会高分反复选择同一个不稳定父节点。
- 改动点：
  - `Connected` 时立即把 `pending_parent` 缓存进 `uplink`，不再等 property discovery 才知道父节点身份。
  - 连接/配对/鉴权/SSAP exchange/find structure 增加日志，方便定位断在第几步。
  - uplink 在 `handle_ready=false` 时断开，按 `disconnect_before_ready` 记录父节点失败，并清理旧配对。
  - 自动 `AUTO-UP` 只在 `uplink.connected && handle_ready` 后发送，避免重连阶段刷误导性的 `DST=1 ERR=unreachable`。
- 影响文件：
  - `inc/ST_test_internal.h`
  - `src/ST_test.c`
  - `src/ST_test_link.c`
  - `src/ST_test_relay.c`
  - `src/ST_test_leaf.c`
- 现场预期：
  - 如果 root 重启后仍断链，应能看到 `conn state`、`pair/auth`、`uplink exchange/find` 或 `uplink setup failed reason=...` 指明真正断点。
  - 断在 ready 前的父节点会被短期降权，relay 会更容易选择其它可用父节点恢复树。

### 9) 连接请求成功但没有有效连接回调时的超时兜底

- 动机：
  - 现场出现 `connect parent node=... ret=0x0` 后没有 `[Connected]`，随后又收到 `conn_id=ffff` 的断开事件。
  - 这种情况下应用层没有拿到有效 conn_id，原逻辑可能一直等回调，或者被新一轮扫描覆盖 pending 父节点。
- 改动点：
  - 新增 `SLE_TREE_CONNECT_CALLBACK_TIMEOUT_MS`，连接请求成功后开始计时。
  - 如果超时仍未进入 `uplink.connected`，打印 `parent connect timeout ...`，记录父节点失败并重新扫描。
  - 有 pending 连接等待回调时，禁止启动新一轮扫描，避免覆盖正在连接的父节点。
- 影响文件：
  - `inc/ST_test_internal.h`
  - `src/ST_test.c`
  - `src/ST_test_link.c`
  - `src/ST_test_relay.c`
  - `src/ST_test_leaf.c`

---

## 后续约定

从现在开始，每次涉及以下任一项，我都会追加到本文件：

- 拓扑收敛逻辑（选父、迁移、重连）
- 广播/扫描参数与候选过滤
- 路由学习与拓扑拼树
- 容量上限与角色行为
- 导致现场日志语义变化的调整
