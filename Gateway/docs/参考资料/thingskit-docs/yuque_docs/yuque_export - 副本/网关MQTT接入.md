---
title: 网关MQTT接入
---

## 入门介绍
在物联网中，网关的作用是将那些本身不能直接连接平台的设备，通过网关的中转，让设备接入平台。网关起到的作用是数据转发和协议转换。

网关和平台的通信主要分为：

+ 网关设备自身和平台的通信，例如：上报网关自身的设备状态和属性，接收平台对网关的控制指令等。
+ 网关子设备和平台的通信，例如：网关连接的Zigbee温湿度传感器向平台上报温湿度，以及网关连接的RS485/Modbus继电器数据或传感器数据，接收平台下发的实时指令。这些子设备的通信都需要经过网关的转发。

这一节，我们主要介绍的是利用网关如何实现子设备和平台的通信，ThingsKit提供了一套网关专用的MQTT协议，包括独立的主题和消息格式。

## 为网关添加子设备
在使用网关MQTT协议时，有两种方式确定绑定关系。

1、手动在ThingsKit平台上创建子设备和网关设备绑定关系。

2、通过满足的json格式，上报遥测数据，系统会自动创建该网关设备的子设备。

:::color4
💡注意：网关在上报不同类型设备数据时，其topic和数据格式不一样。

**网关上报本身的数据时（与直连设备一致）：**

topic: **v1/devices/me/telemetry**

数据格式：**{"DO1":true}**

**网关上报子设备（传感器）的数据时：**

topic: **v1/gateway/telemetry**

数据格式：**{"网关子设备":[{"temperature":35.2}]}**

:::

### 设备类型
首先，网关和子设备都是**设备**，它们的区别仅仅在于所属的**设备类型**不同：

+  网关设备必须归属于**网关设备**设备类型。
+  网关子设备必须归属于**网关子设备**设备类型。

### 网关子设备
使用时，只需要单击编辑，重新绑定一下**网关设备**，来辨别两者之间的关系。如下图：

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1756092111055-2809d360-dd27-4d91-99db-16168b70e833.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_13%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="285.71429868944705" title="" crop="0,0,1,1" id="u081a0e50" class="ne-image">

网关子设备，如下图：

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1756092148534-b082b3e4-4324-4cee-92c6-70b607b79295.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_13%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="288.25398134446436" title="" crop="0,0,1,1" id="u03715e1e" class="ne-image">

关联关系如下图：

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1756092213124-d1f82c43-52e2-43ca-8d7a-0ce5e638a8b5.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u5c4af8f8" class="ne-image">

## 设备连接API
为了通知ThingsKit设备已连接到网关，需要发布以下消息：

```plain
v1/gateway/connect
```

```json
{"device":"Device A"}
```

其中**Device A**是您的设备名称。

一旦收到，ThingsKit将查找或创建具有指定名称的设备。此外，ThingsKit将向此网关发布有关特定设备的新属性更新和RPC命令的消息。

## 设备断开API
为了通知ThingsKit设备与网关断开连接，需要发布以下消息：

```plain
v1/gateway/disconnect
```

```json
{"device":"Device A"}
```

## 遥测上传接口
为了将设备遥测发布到ThingsKit服务器节点，请将PUBLISH消息发送到以下主题：

```plain
v1/gateway/telemetry
```

:::color4
💡提示

下方的Device_A_Sub是网关子设备名称。

:::

数据上报格式如下：**ts**是以毫秒为单位的unix时间戳。

```json
{
  "Device_A_Sub": [
    {
      "ts": 1746599658000,
      "values": {
        "temperature": 42,
        "humidity": 80
      }
    }
  ],
  "Device_B_Sub": [
    {
      "ts": 1746599658000,
      "values": {
        "temperature": 42,
        "humidity": 80
      }
    }
  ]
}
```

或者下面另一种格式

```json
{
  "Device_A_Sub": [
    {
      "temperature": 42,
      "humidity": 80
    }
  ]
}
```

## 调用控制接口
### 服务器端RPC
为了从服务器订阅RPC命令，发送SUBSCRIBE消息到以下主题：

```plain
v1/devices/me/rpc/request/+
```
