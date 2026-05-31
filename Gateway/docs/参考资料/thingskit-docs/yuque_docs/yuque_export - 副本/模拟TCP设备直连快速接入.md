---
title: 模拟TCP设备直连快速接入
---

:::color4
💡 提示

使用直连设备方式接入设备适用于单一对应上报数据点或需要快速对接设备，本文档演示如何快速在平台上展示设备上报原始报文。

:::

# 准备工作
## <font style="color:rgb(38, 38, 38);">TCP设备模拟工具下载</font>
<img src="https://cdn.nlark.com/yuque/0/2023/png/36214471/1689326407517-7ce5aa99-b094-479f-b293-3ba93bf747b4.png?x-oss-process=image%2Fformat%2Cwebp%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_28%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="996" title="" crop="0,0,1,1" id="cxgpJ" class="ne-image">

<font style="color:rgb(38, 38, 38);">NetAssist网络调试助手，是Windows平台下开发的TCP/IP网络调试工具，集TCP/UDP服务端及客户端于一体，是网络应用开发及调试工作必备的专业工具之一，可以帮助网络应用设计、开发、测试人员检查所开发的网络应用软/硬件的数据收发状况，提高开发速度，简化开发复杂度，成为TCP/UDP应用开发调试的得力助手。</font>

<font style="color:rgb(38, 38, 38);">模拟软件下载地址：</font>

[NetAssist网络调试助手-软件工具-野人家园](http://www.cmsoft.cn/resource/102.html)

# <font style="color:rgb(38, 38, 38);">平台创建模拟设备</font>
## <font style="color:rgb(38, 38, 38);">创建转换脚本</font>
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433486666-f6f75cd3-4d56-4df0-9fc4-449f9c997494.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1459.4285714285713" title="" crop="0,0,1,1" id="u86163f2f" class="ne-image">

```shell
var teleData = {};
var params = msg['params'];
/*物模型数据(可选)：原始数据*/
teleData.source = params;
/*直连设备：tempVal是产品物模型中所定义属性的标识符*/
var tempVal = params;
/*物模型测试标识符*/
teleData.test = (parseInt('0x'+tempVal.substr(6, 4)));
msg.datas = teleData;
/*必填：true表示设备上报的遥测数据，false表示命令下发的响应数据*/
msg.telemetry = true;
delete msg.params
/*必填：true表示设备上报的遥测数据，false表示命令下发的响应数据*/
return {msg: msg};
```

:::color4
💡 注意

创建的脚本状态为启用时才能被调用。

:::

## <font style="color:rgb(38, 38, 38);">创建直连设备产品</font>
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433646469-eac7b745-5553-4042-86bb-5be3f95cd637.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="u2cbbfd4a" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433678368-066ee871-9ce6-44a2-b6bf-9b40492c3c24.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1446.857142857143" title="" crop="0,0,1,1" id="u902073bc" class="ne-image">

:::color4
💡 提示

与网关设备不同，直连设备只需要创建自己的脚本即可，支持modbusRTU自动解析。

:::

## 创建物模型
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433788758-55b95831-8fb9-4b92-8e0c-4430111449ae.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1454.2857142857142" title="" crop="0,0,1,1" id="u11c8b78b" class="ne-image">

:::color4
💡 提示

创建物模型按键需要点击“编辑物模型切换”。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433871949-e807934a-c44b-4ace-8ec1-f0d85180b6dc.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="u8ff8abf7" class="ne-image">

:::color4
💡 提示

直连设备的凭证即Access Token值，在后面用模拟工具是需要按照修改后的凭证发送注册包，注册包内容即Access Token值。

:::

## 创建直连设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433977647-0dee4104-7222-48cb-b045-19f32ca9ec82.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1460.5714285714287" title="" crop="0,0,1,1" id="u98bae5c2" class="ne-image">

# <font style="color:rgb(38, 38, 38);">模拟TCP网关设备接入</font>
## 复制设备凭证
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753434108189-c2794569-bda4-4a2f-bb04-6460d59cf367.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1455.4285714285713" title="" crop="0,0,1,1" id="u13f99628" class="ne-image">

## <font style="color:rgb(38, 38, 38);">使用工具连接平台</font>
:::color4
💡提示

tcp协议默认端口为8088。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753349725781-7b5e3344-d44d-4521-9fb3-498c7b85e758.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="666.8571428571429" title="" crop="0,0,1,1" id="ua1568234" class="ne-image">

## <font style="color:rgb(38, 38, 38);">使用工具下发数据</font>
<font style="color:rgb(38, 38, 38);">首先要用测试工具发送注册包，然后使用注册工具发送测试数据：</font>

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753350191102-c62c25cd-7ea3-4e96-9cba-bf5bc2de264c.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_56%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1125.7142857142858" title="" crop="0,0,1,1" id="u81408331" class="ne-image">

:::color4
💡 注意

使用测试工具发送注册包时需要选择ASCII码发送，且发送注册包内容要与凭证一致。

source物模型属性会直接显示设备端上报的数据方便调试。

:::
