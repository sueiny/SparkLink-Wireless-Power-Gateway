---
title: 设备UDP接入
---

## 入门介绍
### UDP基础知识
<font style="color:rgb(77, 77, 77);">UDP是</font>**<font style="color:rgb(77, 77, 77);">User Datagram Protocol</font>**<font style="color:rgb(77, 77, 77);">（用户数据协议）的简称，是一种无连接的协议，该协议工作在</font>OSI模型<font style="color:rgb(77, 77, 77);">中的第四层（传输层），处于IP协议的上一层。传输层的功能就是建立“端口到端口”的通信，</font>**<font style="color:rgb(77, 77, 77);">UDP提供面向事务的简单的不可靠信息传送服务。</font>**

**接下来来看UDP与TCP的区别：**

| UDP | TCP |
| --- | --- |
| 面向无连接 | 面向连接（即需要建立连接） |
| 基于数据报（发送数据时会直接打上UDP头部将整个报文发送出去） | 面向字节流（发送数据时会将数据分解为多个小的数据报文进行发送） |
| 传输数据可能存在丢包 | 有三次握手可以保证数据传输的可靠性 |
| 无法保证数据顺序 | 保证数据顺序 |
| 支持一对一、一对多、多对多通讯 | 只支持点对点通讯 |
| 无拥塞机制 | 有拥塞机制 |
| 头部8个字节 | 头部20-60个字节 |
| 要求实时性高，准确度低 | 要求实时性低，准确度高 |


:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">ThingsKit支持设备直接通过UDP接入平台，并支持 JSON、Text、HEX格式的上下行消息，平台中还提供了脚本函数功能，为更复杂的设备通信提供了便利性。</font>

:::

**UDP设备的常见网络配置：**

+ IP：127.0.0.1
+ port：8088

## 设备身份认证
UDP设备的上报样式：

+ 【注册包】+【数据包】
+ 【注册包】

### UDP注册包
<font style="color:rgb(38, 38, 38);">注册包对平台而言则是访问令牌“AccessToken”。只要保证设备端的注册包与平台端的访问令牌一致。  
</font><font style="color:rgb(38, 38, 38);">注册包是作为设备端与平台鉴权的方式，如果常见的DTU厂家，都有提供配置软件来设置。  
</font><font style="color:rgb(38, 38, 38);">当设备和平台成功建立UDP连接后，设备必须马上向平台发送身份信息，完成身份认证。若设备端在一定时间内未发送身份信息，平台会自动断开设备的UDP连接。  
</font><font style="color:rgb(38, 38, 38);">在使用UDP透传方式的网关或DTU中，同样可以使用该注册包连接到平台。</font>

**<font style="color:rgb(38, 38, 38);">示例：UDP模拟工具注册包设置：</font>**

<img src="https://cdn.nlark.com/yuque/0/2023/png/36214471/1689750524116-071d11a2-490d-4e91-a7eb-1c10ac050970.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_28%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="664" title="" crop="0,0,1,1" id="u6001b925" class="ne-image">

### <font style="color:rgb(38, 38, 38);">UDP心跳包</font>
<font style="color:rgb(38, 38, 38);">当设备和平台建立UDP连接并完成身份认证后，便可以相互收发消息。但是，如果相当长一段时间内没有消息通信，双方如何判断对方仍然在线呢？因为UDP对于一些非正常的连接断开是无法侦测到的，比如设备断电、网线断掉等。</font>

<font style="color:rgb(38, 38, 38);">因此，对于消息通信间隔较长的应用场景，为了让双方尽早的知道连接是否已经断开，从而实现重连，就需要有UDP保活机制，这是通过设备定期发送心跳包来实现的。</font>

<font style="color:rgb(38, 38, 38);">但是，大多数物联网通信场景的数据上报间隔时间并不长，所以也可以起到保活的目的，心跳包不是必须的，用户可根据需求使用心跳包。</font>

## <font style="color:rgb(38, 38, 38);">数据流转换</font>
<font style="color:rgb(38, 38, 38);">平台提供的UDP接入方式，是需要您对UDP通道所属的自定义数据流，设置相应的消息规则，实现自定义数据和设备属性之间的解析和处理。  
</font><font style="color:rgb(38, 38, 38);">数据上报到平台，则需要通过平台提供的转换函数进行，转码解析处理。  
</font><font style="color:rgb(38, 38, 38);">如某厂家的温湿度传感器，上报到平台的数据为：</font>

```plain
010304026C00883BF0
```

平台端，可通过转换函数解析数据：

```json
var teleData = {};
var params = msg['params'];
/*物模型数据(可选)：原始数据*/
teleData.source = params;
/*直连设备：tempVal是产品物模型中所定义属性的标识符*/
var tempVal = params;
/*物模型温度标识符得出的值136除以10*/
teleData.temp = parseFloat(Integer.parseInt(tempVal.substring(10, 14), 16))/10;
/*物模型湿度标识符*/
teleData.wet = parseFloat(Integer.parseInt(tempVal.substring(6, 10), 16))/10;
/*物模型开关标识符*/
teleData.switch = Integer.parseInt(tempVal.substring(7, 8), 16);
msg.datas = teleData;
/*必填：true表示设备上报的遥测数据，false表示命令下发的响应数据*/
msg.telemetry = true;
msg.remove('params');
/*必填：true表示设备上报的遥测数据，false表示命令下发的响应数据*/
return {msg: msg};
```

解析结果为：

```json
{
  "datas": {
    "source": "010304026C00883BF0",
    "temp": 13.6,
    "wet": 62,
    "switch": 2
  },
  "telemetry": true
}
```

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1742977326582-af42e0b0-d908-4310-bd7b-1e3ae4ea2612.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_54%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1910" title="" crop="0,0,1,1" id="u157d61a8" class="ne-image">

## 数据上报
<font style="color:rgb(38, 38, 38);">设备接入时，咱们得先创建UDP协议的产品，产品创建时，需要绑定对应的转换脚本。</font>

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1756104278162-600adaa2-843e-4371-9c2d-71960a0f1fb2.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u6b2890d0" class="ne-image">

<font style="color:rgb(38, 38, 38);">一旦完成上边的UDP绑定自定义数据流，设备端就可以通过UDP协议发送符合负载格式的数据，例如：类似属性消息结构的JSON消息，或者</font>**<font style="color:rgb(38, 38, 38);">自定义的HEX消息</font>**<font style="color:rgb(38, 38, 38);">。  
</font><font style="color:rgb(38, 38, 38);">平台收到UDP自定义数据上报后，则通过规则引擎来对数据做各种解析和处理，平台提供了函数等可编程方式。</font>

:::color4
💡 提示

平台8088端口同时监听UDP协议数据，平台产品类型只显示TCP的问题后面会修改。

:::

## 数据下发
<font style="color:rgb(38, 38, 38);">接下来就是平台下发命令到设备端自定义数据，可以通过如下方式：</font>

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1756104596758-4b0bdb50-3f4c-4b4c-9148-524ddc44ed20.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="ufb10bf12" class="ne-image">

规则引擎的命令下发如下图：

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1756104852780-255155b7-67a1-40d4-ac86-3b83e2a37516.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_36%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="805.7143223042408" title="" crop="0,0,1,1" id="u763ad39c" class="ne-image">

---
