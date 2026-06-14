---
title: MQTT产品
---

:::color4
💡注意：

网关上报网关子设备的数据时，**不允许 **修改Topic地址，即使修改也是其本身的Topic上报地址。

网关和直连设备上报自身的数据时，**允许 **修改Topic地址。

支持：**Json**、**Protobuf**

       不支持：**HEX**、**CBOR**、**MsgPak**、**Plaintext**、**Base64**

:::

# 前置条件
需要有登录账号并先了解产品的定义，具体可以通过[演示账号&学习路线](https://yunteng.yuque.com/avshoi/v2xdocs/skbkmf0o82nsoyrp) 和 [基本概念](https://yunteng.yuque.com/avshoi/v2xdocs/to3rigum9gyaukab#xCOHe) 查看。

# 创建MQTT产品
## 遥测Topic
**默认发布Topic说明：**

网关和直连设备上报自身数据：**v1/devices/me/telemetry （可修改）**

网关上报网关子设备的数据：**v1/gateway/telemetry （不可修改）**

**默认订阅Topic说明：**

平台命令下发主题：**v1/devices/me/rpc/request/+ （不可修改）**

## 客户端属性Topic
**默认发布Topic说明：v1/devices/me/attributes （可修改）**

**默认订阅Topic说明：v1/devices/me/attributes （可修改）**

## **自定义Topic规则**
**自定义Topic说明：**

+ 支持单级[+]和多级[#]通配符。
+ [+] 适用于任何主题过滤级别。例如：v1/devices/+/telemetry or +/devices/+/attributes。
+ [#]可以替换主题筛选器本身，并且必须是主题的最后一个符号。例如：# or v1/devices/me/#。

<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755510177989-16e92539-a810-4fc6-b529-c3a33c60aa18.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2560" title="" crop="0,0,1,1" id="u401b7d9f" class="ne-image">

# 添加物模型
[创建物模型](https://yunteng.yuque.com/avshoi/v2xdocs/toqngtefr0yeq2pi#AbBEL)


