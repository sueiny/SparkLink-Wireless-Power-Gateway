---
type: moc
area: gateway
tags:
  - gateway/index
---

# Gateway 文档总览

这个目录用于保存 Gateway 项目内部文档。主线目标是支持项目复盘、答辩介绍、面试回忆、技术沉淀和后续 code review。

## 主线入口

- [[索引/01-项目说明索引]]
- [[索引/02-项目复盘索引]]
- [[索引/03-技术沉淀索引]]
- [[索引/04-CodeReview索引]]
- [[索引/05-学习技能索引]]
- [[索引/06-参考资料索引]]
- [[新增内容归档指令]]

## 如何讲清楚这个项目

1. [[00_项目说明/作品介绍|作品介绍]]
2. [[00_项目说明/系统架构与技术栈|系统架构与技术栈]]
3. [[00_项目说明/设备拓扑图|设备拓扑图]]
4. [[10_项目复盘/gatewayd项目复盘|gatewayd项目复盘]]
5. [[20_技术沉淀/gatewayd框架理解|gatewayd框架理解]]
6. [[20_技术沉淀/Gateway模块说明书|Gateway模块说明书]]
7. [[20_技术沉淀/sle_data_app使用说明|sle_data_app使用说明]]
8. [[20_技术沉淀/sle_data_app命令对接阅读理解|sle_data_app命令对接阅读理解]]
9. [[20_技术沉淀/Modbus寄存器仿真规格|Modbus寄存器仿真规格]]
10. [[30_CodeReview/Gateway全量模块性能CodeReview|Gateway全量模块性能CodeReview]]
11. [[30_CodeReview/Gateway全量模块可维护性CodeReview|Gateway全量模块可维护性CodeReview]]
12. [[30_CodeReview/Gateway可维护性整改路线|Gateway可维护性整改路线]]
13. [[10_项目复盘/Gateway上板测试记录|Gateway上板测试记录]]

## 模块主文档

模块工程细节优先放在源码旁的 docs 中，`Gateway/docs` 只保留复盘、学习和索引入口。

- `gatewayd/docs/README.md`
- `gatewayd/docs/gatewayd框架理解.md`
- `gatewayd/docs/SLE数据源替换MockDataSource计划.md`
- `gatewayd/docs/Modbus寄存器仿真规格.md`
- `sle_data_app/docs/README.md`
- `sle_data_app/docs/使用说明.md`
- `sle_data_app/docs/命令对接阅读理解.md`
- `sle_data_app/docs/架构分析与改造计划.md`
- `sle_data_app/docs/Modbus寄存器仿真规格.md`

## 项目一句话

基于星闪的无线电力透传系统，面向 RS485/Modbus 设备无线化改造，通过 DTU 采集、星闪组网、边缘网关汇聚和平台展示，实现低成本部署、统一上云、边缘自治和后续智能化扩展。

## 维护说明

旧目录已按新分区整理。后续新增内容不要再放到旧的 `项目文档`、`gatewayd项目`、`参考资料` 根分区，优先进入六个标准分区。
