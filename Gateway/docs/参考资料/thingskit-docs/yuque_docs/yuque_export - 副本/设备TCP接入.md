---
title: 设备TCP接入
---

## 入门介绍
### TCP基础知识
**TCP协议全称是传输控制协议是一种面向连接的、可靠的、基于字节流的传输层通信协议。**有三次握手可以保证数据传输的可靠性。TCP提供超时重发，丢弃重复数据，检验数据，流量控制等功能，保证数据能从一端传到另一端。

<img src="https://static.thingskit.com/iotdocs/img/image-20230421143508256.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_21%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="739" title="" crop="0,0,1,1" id="PoBtP" class="ne-image">

TCP是由TCP头部和TCP数据两个部分组成。头部是由上图标识的一些字段组成，对上图字段的分析如下：

+  Source port（源端口）：源主机应用程序所使用的端口号;
+  Destination port（目的端口）：目的主机使用的端口号;
+  Sequence Number(序列号)：用于标识从发送端发出的不同的TCP数据段的序号。数据段在传输过程中他们的顺序会发生变化，因此接收端需要根据序列号来对数据进行重组。
+  Acknowledge Number（确认序列号）：用于标识接收端确认收到的数据段。确认序列号为成功收到的数据序列号+1。
+  Header length（头部长度）:标识头部占32bit字的数目，他能表达的TCP头部最大长度为60字节。
+  Window（窗口大小）：表示接收端期望通过单次确认而收到的数据大小。该机制通常用于流量的控制。
+  Checksum（校验和）：校验整个TCP字段，包括TCP头部和TCP数据。该值由发送端计算和记录并由接收端进行验证。

**接下来来看TCP与UDP的区别：**

| TCP<font style="background-color:#EFF0F0;"></font> | UDP |
| --- | --- |
| 面向连接（即需要建立连接） | 面向无连接 |
| 面向字节流（发送数据时会将数据分解为多个小的数据报文进行发送） | 基于数据报（发送数据时会直接打上UDP头部将整个报文发送出去） |
| 有三次握手可以保证数据传输的可靠性 | 传输数据可能存在丢包 |
| 保证数据顺序 | 无法保证数据顺序 |
| 只支持点对点通讯 | 支持一对一、一对多、多对多通讯 |
| 有拥塞机制 | 无拥塞机制 |
| 头部20-60个字节 | 头部8个字节 |
| 要求实时性低，准确度高 | 要求实时性高，准确度低 |


:::color4
💡 提示

ThingsKit支持设备直接通过TCP接入平台，并支持 JSON、Text、HEX格式的上下行消息，平台中还提供了脚本函数功能，为更复杂的设备通信提供了便利性。

:::

**TCP设备的常见网络配置**：

+ IP：127.0.0.1
+ port: 8088

## 设备身份认证
TCP  设备的上报样式：

+ 【注册包】+【数据包】
+ 【注册包】

### TCP注册包
注册包对平台而言则是访问令牌“AccessToken”。只要保证设备端的注册包与平台端的访问令牌一致。

注册包是作为设备端与平台鉴权的方式，如图常见的DTU厂家，都有提供配置软件来设置。

当设备和平台成功建立TCP连接后，设备必须马上向平台发送身份信息，完成身份认证。若设备端在一定时间内未发送身份信息，平台会自动断开设备的TCP连接。

在使用TCP透传方式的网关或DTU中，同样可以使用该注册包连接到平台。

**示例：IoTRouter的DTU注册包设置**如下：

<img src="https://cdn.nlark.com/yuque/0/2023/jpeg/1196280/1687230547466-75486576-1014-4c4e-ac76-2b245353e519.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1926" title="" crop="0,0,1,1" id="u92e23630" class="ne-image">

### TCP心跳包
当设备和平台建立TCP连接并完成身份认证后，便可以相互收发消息。但是，如果相当长一段时间内没有消息通信，双方如何判断对方仍然在线呢？因为TCP对于一些非正常的连接断开是无法侦测到的，比如设备断电、网线断掉等。

因此，对于消息通信间隔较长的应用场景，为了让双方尽早的知道连接是否已经断开，从而实现重连，就需要有TCP保活机制，这是通过设备定期发送心跳包来实现的。

然后，大多数物联网通信场景的数据上报间隔时间并不长，所以也可以起到保活的目的，心跳包不是必须的。

## 数据流转换
平台提供的TCP接入方式，是需要您对TCP通道所属的自定义数据流，设置相应的消息规则，实现自定义数据和设备属性之间的解析和处理。

数据上报到平台，则需要通过平台提供的转换函数进行，转码解析处理。

如某厂家的风速和湿度传感器，上报到平台的数据为：

```plain
010304026C00883BF0
```

平台端，则通过转换函数：

```javascript

  var teleData = {};
  var params = msg['params'];
  /*物模型数据(可选)：原始数据*/
  teleData.source = params;
  /*直连设备：tempVal是产品物模型中所定义属性的标识符*/
  var tempVal = params;
  /*物模型温度标识符*/
  teleData.speed = (parseInt('0x'+tempVal.substr(10, 4))*0.1).toFixed(2);
  /*物模型湿度标识符*/
  teleData.wet = (parseInt('0x'+tempVal.substr(6, 4))*0.1).toFixed(2);
  /*物模型开关标识符*/
  teleData.switch = parseInt('0x'+tempVal.substr(7, 1));
  msg.datas = teleData;
  /*必填：true表示设备上报的遥测数据，false表示命令下发的响应数据*/
  msg.telemetry = true;
  delete msg.params
  /*必填：true表示设备上报的遥测数据，false表示命令下发的响应数据*/
  return {msg: msg};
```

解析结果为：

```json
{
  "msg": {
    "datas": {
      "source": "010304026C00883BF0",
      "temp": "13.60",
      "wet": "62.00",
      "switch": 2
    },
    "telemetry": true
  },
  "metadata": {
    "deviceType": "default",
    "deviceName": "Test Device",
    "ts": "1758525124454"
  },
  "msgType": "POST_TELEMETRY_REQUEST"
}
```

TCP测试脚本转换功能页面：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758525196885-27f456e5-af5d-4ae7-ab7b-081ed4960085.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1917" title="" crop="0,0,1,1" id="u22d5b156" class="ne-image">

## 数据上报
设备接入时，咱们得先创建TCP协议的产品，产品创建时，需要绑定对应的转换脚本，选择鉴权方式，注册包或者数据携带。

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1756102824601-a9b53c70-763f-47d1-8eca-ecdf20fd722a.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u3cc0d2d6" class="ne-image">

一旦完成上边的TCP绑定自定义数据流，设备端就可以通过TCPsocket发送符合负载格式的数据，例如：类似属性消息结构的JSON消息，或者**自定义的HEX消息**。

平台收到TCP自定义数据上报后，则通过规则引擎来对数据做各种解析和处理，平台提供了函数等可编程方式。

## 数据下发
接下来就是平台下发命令到设备端自定义数据，可以通过如下方式：

+  设备详情中的命令下发；
+  规则引擎中的设备联动命令下发。

设备详情中的命令下发见下图（下发的hex命令必须为双数）：

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1756103020087-001d6fb8-f17a-47a2-b8c0-35f8f2e14d4e.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="ufa9657d6" class="ne-image">

规则引擎的命令下发如下图：（可自定义下发和服务定义下发）

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758525456246-2cf4ca7d-5ec5-46b9-979d-8660c9430a24.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_26%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="897" title="" crop="0,0,1,1" id="u2d43ceba" class="ne-image">
