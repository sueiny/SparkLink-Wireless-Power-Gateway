---
title: 模拟UDP直连设备接入
---

# 准备工作
## UDP设备模拟工具下载
<img src="https://cdn.nlark.com/yuque/0/2023/png/36214471/1689559023296-8c673922-1a20-4008-95cc-12f7ae2459fe.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_28%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="664" title="" crop="0,0,1,1" id="u53b08acb" class="ne-image">  
NetAssist网络调试助手，是Windows平台下开发的TCP/IP网络调试工具，集TCP/UDP服务端及客户端于一体，是网络应用开发及调试工作必备的专业工具之一，可以帮助网络应用设计、开发、测试人员检查所开发的网络应用软/硬件的数据收发状况，提高开发速度，简化开发复杂度，成为TCP/UDP应用开发调试的得力助手。

[NetAssist网络调试助手 V5.0.7-软件工具-野人家园](http://www.cmsoft.cn/resource/102.html)

# 平台创建模拟设备
## 创建转换脚本
<font style="color:rgb(38, 38, 38);">首先假设上传的数据为modbus返回值：010302004A39B3</font>

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753683663113-e966c004-3251-4440-958f-1dec4d80b4a0.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1613.9683272634988" title="" crop="0,0,1,1" id="ud7e1e99d" class="ne-image">

```json
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
💡 提示

确定了返回数据格式后才能创建对应的脚本进行解析，本测试脚本会将上传至平台的数据中寄存器的0x004A转换为十进制数74。

创建的脚本默认状态为关闭，需要手动启用才能被调用。

:::

## 创建直连产品
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753684288593-88f52190-f926-4199-8fcc-56f4198273f1.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u2ca47872" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753684205223-7244dd70-2fd5-4d68-966f-7ad7a11e2d8f.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1623.4921372198137" title="" crop="0,0,1,1" id="u41365212" class="ne-image">

:::color4
💡 提示

这里使用接入协议为TCP/UDP，也可以接入TCP协议的设备。

:::

## 创建直连设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753686166527-f5cfbcd9-2aa2-4a75-953b-b2c6f0e1192e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="ud78f65c7" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">创建网关设备后修改了网关设备的凭证即Access Token值，在后面用模拟工具是需要按照修改后的凭证发送注册包，注册包内容即Access Token值。</font>

:::

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">创建设备时所填的设备表示即modbus返回数据中的地址位，本次测试返回为01。</font>

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753686204795-d55017bb-5eb0-4c7f-b200-7601bc1b17cd.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1608.25404128971" title="" crop="0,0,1,1" id="ufd2b9889" class="ne-image"><img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753686266947-0cdde82c-eab0-48a7-bb8d-3ee0acc5fb1d.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="uc61c8820" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753686456044-90ae89bf-f82b-4741-abdf-25c4197bff6d.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u7aa213c0" class="ne-image">

## 创建物模型
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753686697617-83ec9281-ccf9-4a63-91a2-40785f15c8cd.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="ub216835a" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">创建物模型按键需要点击“编辑物模型切换”。</font>

:::

# 模拟UDP设备接入
## 使用工具连接平台
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753693527577-fe52a7e6-d9d5-4a40-8536-30250e70ebaf.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_49%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1097.777827631231" title="" crop="0,0,1,1" id="uab611242" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">如果无法连接平台请检查对应端口是否占用或检查网络问题。</font>

:::

:::color4
💡 注意

<font style="color:rgb(38, 38, 38);">测试中使用服务器地址以及端口需要根据实际情况填写，该软件在打开PC对应端口后“远程主机”内填写地址和端口。</font>

<font style="color:rgb(38, 38, 38);">体验账号</font>

<font style="color:rgb(38, 38, 38);">服务器地址：demo.thingskit.com或47.101.144.59</font>

<font style="color:rgb(38, 38, 38);">端口号：8088</font>

:::

## 使用工具下发数据
<font style="color:rgb(38, 38, 38);">首先要用测试工具发送注册包，然后使用注册工具发送测试数据：</font>

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753693590072-3b98125e-d087-40c4-9290-8ea242e2b53e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_49%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1097.777827631231" title="" crop="0,0,1,1" id="u8a11ef34" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">使用测试工具发送注册包时需要选择ASCII码发送，且发送注册包内容要与凭证一致。</font>

:::

:::color4
💡 注意

<font style="color:rgb(38, 38, 38);">使用测试工具发送数据是需要选择HEX发送。</font>

<font style="color:rgb(38, 38, 38);">使用模拟工具发送数据至平台后，设备物模型显示的值就是0x4A的十进制数74。</font>

:::
