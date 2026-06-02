# `sle_tree_test`

`sle_tree_test` 是基于 FBB WS63 原生 SLE/OSAL 接口实现的动态树状网络 demo。

当前用户侧只需要烧录两类角色：

- `root`
- `relay node`

`relay node` 同时具备上行和下行能力：它可以连接父节点，也可以继续接入子节点。某个 relay node 当前没有孩子时，它在拓扑上自然就是叶子位置，不再需要单独烧录 leaf 固件。

## 角色

在 `menuconfig` 里选择一个角色编译：

- `CONFIG_SAMPLE_SUPPORT_SLE_TREE_ROOT_SAMPLE`
- `CONFIG_SAMPLE_SUPPORT_SLE_TREE_RELAY_SAMPLE`

## 连接逻辑

- `root` 作为根节点广播 `TREE_ROOT`
- `relay node` 扫描并连接 `root` 或已经入树的其他 `relay node`
- `relay node` 连上父节点并完成 handle discovery 后，会继续广播 `TREE_RELAY`
- 新 relay node 按 `depth + free_slots + RSSI + last_parent - failure_count` 打分选择父节点
- 已入网 relay node 会周期性优化选父，必要时主动迁移到更优父节点

## 拓扑上报

每个 relay node 周期性向 root 上报自己的局部拓扑摘要：

- 自己的 `node_id`
- 自己直连的 children

root 汇总这些局部摘要后，周期性打印 ASCII 树：

```text
[sle_tree] topo tree begin
1
|- 18
|  |- 49
|  `- 51
`- 21
   |- 55
   `- 56
[sle_tree] topo tree end
```

## 一对多前提

每块板都必须有不同的 SLE MAC。

当前 sample 启动时优先读取 `NV_ID_SYSTEM_FACTORY_SLE_MAC`，无效时回退到代码里的静态 MAC。建议开发阶段用 AT 写 NV SLE MAC：

```text
AT+EFUSEMAC=02:00:00:00:00:11,3
```

## UART

sample 使用系统默认 `bus0`。普通树网测试命令格式：

```text
<dst_node_id> <HEX_PAYLOAD>
```

示例：

```text
17 010203AABB
```

AT 命令以 `AT` / `AT+` 开头时会转发给系统 AT 通道，例如：

```text
AT+RST
AT+EFUSEMAC?
```

## 自动流量

开启 `CONFIG_SLE_TREE_TEST_ENABLE_AUTO_TRAFFIC` 后：

- relay node 周期性向 root 发送 `AUTO-UP`
- root 周期性向第一条已知路由发送 `AUTO-DOWN`

默认上行目标节点是 root：

```text
CONFIG_SLE_TREE_TEST_AUTO_UPLINK_DST_NODE_ID=1
```
