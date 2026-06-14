---
title: Docker微服务部署V2.0.0即后续版本
---

:::color3
💡提示：

thingskit1.x版本部署包与当前部署文档结构差异很大，请对照版本参考文档。本文档基于x86架构服务器部署，如果需要对arm架构服务器部署，请先咨询相关人员。

:::

<font style="color:rgb(38, 38, 38);">平台更新版本指导：</font>

[平台更新版本指导](https://yunteng.yuque.com/avshoi/v2xdocs/oig334tixekh1twm)

# <font style="color:rgb(38, 38, 38);">端口清单</font>
[默认端口V2](https://yunteng.yuque.com/avshoi/v2xdocs/nr3n8nzk56sk4nmi#XX3mZ)

# 第1/8步：Docker安装【如果已经安装，可略过】
:::info
💡提示

docker版本不建议用过低版本。

:::

**根据您的操作系统选择**：

[国产系统(Kylin 、openEuler ) Linux](https://yunteng.yuque.com/avshoi/v1xdocs/pc6mq85nntgusa1b)

[Ubuntu Linux](https://yunteng.yuque.com/avshoi/v1xdocs/ombfzh3udmssrpoo)

[CentOS Linux](https://yunteng.yuque.com/avshoi/v1xdocs/xl0p1wko115eixcn)

[Debian Linux](https://yunteng.yuque.com/avshoi/v1xdocs/bw1hge8ak1eu8coi)

# 第2/8步：获取部署包
[微服务部署包管理](https://yunteng.yuque.com/avshoi/v2xdocs/oghfpggynyeai8m6)

# 第3/8步：配置通用环境变量
:::color1
💡 提示

集群内所有节点保持一致的配置。

:::

[微服务修改通用环境变量](https://yunteng.yuque.com/avshoi/v2xdocs/gvsmq1rodmxety13)

# 第4/8步：修改服务节点的环境变量
:::color1
💡 提示

集群内每个服务器节点的配置都不一样，需要登录每个服务器节点，进行单独修改。

:::

## 1/2：获取license证书
:::warning
💡 提示

**部署版**需要为每个部署服务组件【tb-core】的服务器申请license证书。

:::

[部署版获取license证书](https://yunteng.yuque.com/avshoi/v2xdocs/ar8h7zdssg3cdzxk)

## 2/2：修改服务节点环境变量
[微服务修改服务节点的环境变量](https://yunteng.yuque.com/avshoi/v2xdocs/pg9aq67d17k6m8vl)

# 第5/8步：导入Docker的镜像文件
[Docker镜像导入](https://yunteng.yuque.com/avshoi/v2xdocs/wzht6mqzkr0zfw26)

# 第6/8步：安装微服务依赖软件
[微服务依赖软件部署](https://yunteng.yuque.com/avshoi/v2xdocs/gohp8pkbgdauon88)

# 第7/8步：部署微服务组件
平台的服务组件可实际情况部署到多台服务器

[微服务服务组件部署](https://yunteng.yuque.com/avshoi/v2xdocs/oe29vg86y1kggsp5)

<font style="color:rgb(38, 38, 38);">改为jar方式启用：</font>

[源码部署包调整](https://yunteng.yuque.com/avshoi/v2xdocs/mupga1p654ts6gxq)

# 第8/8步：测试ThingsKit物联网平台是否安装成功
:::color3
💡 提示

设备分布等功能调用地区需要获取高德地图api接口配置。

:::

<font style="color:rgb(38, 38, 38);">配置地图流程：</font>

[安装部署问题](https://yunteng.yuque.com/avshoi/v2xdocs/gho03tgxganir968#QDsmZ)

## 创建租户
:::info
💡 提示

<font style="color:rgb(38, 38, 38);">设备的接入需要再超级管理员的账号登录后，创建租户-租户管理员并访问租户账号才能使用。</font>

:::

[租户列表](https://yunteng.yuque.com/avshoi/v2xdocs/yor8wfhncl6wup4l)

## 功能验证清单
[平台基础功能验收清单](https://yunteng.yuque.com/avshoi/v2xdocs/xmdy5eamsg2s0m4l)

# 启用安全协议SSL/TLS(Plus)
[启用安全协议SSL/TLS(Plus)](https://yunteng.yuque.com/avshoi/v2xdocs/qcwa9lm9uhgbw03i)

# 扩展：**<font style="color:rgb(38, 38, 38);">启动nodered支持平台嵌入使用</font>**
:::warning
‼️ 特别注意

该模块属于增值功能，不包含在基础功能内，部署文件需要额外获取。

:::

:::info
💡 提示

<font style="color:rgb(38, 38, 38);">nodered支持平台嵌入使用为1.5.2以及后续版本支持。</font>

:::

```shell
cd /_makeFile/nodered
ddocker-compose up -d
```
