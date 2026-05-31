# Gateway Code Review 索引

本目录用于保存 `gatewayd` 每个阶段完成后的 code review。

写作约定：

1. 每完成一个阶段，新建一份独立 review。
2. 先写结论和风险，再写学习式解释。
3. 必须说明当前已经完成什么、没有完成什么、哪些只是预留。
4. 必须写验证方式，例如编译、JSON 校验、脚本上传测试。
5. 服务和事件如果只是物模型预留，必须明确写出“不执行、不主动上报”。

当前文档：

| 文件 | 内容 |
|------|------|
| `00_阶段总览_CodeReview.md` | 总体阶段复盘，覆盖已完成和后续任务 |
| `01_物模型与配置对齐_CodeReview.md` | 物模型、配置、拓扑和设备映射 review |
| `02_ThingsKit上传Payload_CodeReview.md` | 网关和子设备上传 payload review |
| `03_凭证模式_CodeReview.md` | access token 与 mqtt_basic 双模式 review |
| `04_命令下发边界_CodeReview.md` | 只解析命令、不执行服务的边界 review |
| `05_后续任务纳入计划_CodeReview.md` | 把之前计划中未完成任务纳入后续阶段 |
| `06_板端数据云端物模型验证_CodeReview.md` | 验证属性数据上传到云端后是否匹配物模型 |
| `07_DTU节点显示异常排查_CodeReview.md` | 排查 DTU 节点角色和在线状态显示问题 |
| `08_BOOL状态字段改ENUM_CodeReview.md` | 将 BOOL 状态字段统一改为 ENUM 并验证 |
