---
title: 模拟TCP设备网关接入
---

:::color4
💡 提示

使用网关+网关子设备方式接入设备适用于对多个传感器接入单个真实网关对接平台。

:::

# 准备工作
## TCP设备模拟工具下载
<img src="https://cdn.nlark.com/yuque/0/2023/png/36214471/1689326407517-7ce5aa99-b094-479f-b293-3ba93bf747b4.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_28%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="664" title="" crop="0,0,1,1" id="u3fef3420" class="ne-image">

NetAssist网络调试助手，是Windows平台下开发的TCP/IP网络调试工具，集TCP/UDP服务端及客户端于一体，是网络应用开发及调试工作必备的专业工具之一，可以帮助网络应用设计、开发、测试人员检查所开发的网络应用软/硬件的数据收发状况，提高开发速度，简化开发复杂度，成为TCP/UDP应用开发调试的得力助手。

模拟软件下载地址：

[NetAssist网络调试助手-软件工具-野人家园](http://www.cmsoft.cn/resource/102.html)

# 平台创建模拟设备
## 创建转换脚本
网关设备脚本：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753670691074-f9cccf51-d37f-4cb9-918f-34e297c8be4c.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1453.142857142857" title="" crop="0,0,1,1" id="uf27b965d" class="ne-image">

网关子设备脚本：

首先假设上传的数据为modbus返回值：010302004A39B3

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753670960658-cb50d62a-035f-4df9-b5e0-212bd55aeb42.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.2857142857142" title="" crop="0,0,1,1" id="u7820abcb" class="ne-image">

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

:::

:::color4
💡 注意

创建的脚本状态为启用时才能被调用。

:::

## 创建网关产品
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753671450598-23ea5147-9ee5-4601-be8e-a2d669b4ed2d.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1461.7142857142858" title="" crop="0,0,1,1" id="u1d3d766f" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753671304642-35e7eebf-e833-4883-9dc5-f12362a64ad5.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1444.5714285714287" title="" crop="0,0,1,1" id="bLXIk" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">自定义类型网关产品需要新增脚本，接入设备协议为modbusRTU可以自行解析。</font>

:::

## 创建网关子设备产品
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753671569590-7ad0e382-cccc-47d8-920a-a6177861593f.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1461.142857142857" title="" crop="0,0,1,1" id="ua8687a9c" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753671618578-45346e18-d052-409d-81df-6e4992b6283c.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1461.7142857142858" title="" crop="0,0,1,1" id="ubf8b5f79" class="ne-image">

:::color4
💡 提示

网关子设备使用的解析脚本需要单独配置，这里以及配置好了使用可以直接选择。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753671708508-46f2cf3f-b46e-4b2b-b7a7-6209fd9d107b.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1458.2857142857142" title="" crop="0,0,1,1" id="ue31689cf" class="ne-image">

:::color4
💡 提示

创建物模型按键需要点击“编辑物模型切换”。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753671959960-1edcc4f8-7131-4ff6-9458-34ed233a23d5.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="u7302c165" class="ne-image">

:::color4
💡 注意

网关子设备创建物模型标识符必须与方法中传递属性名一致。

:::

## 创建网关设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672184412-0201608c-7fdc-4b14-ae30-7bd999675c90.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1455.4285714285713" title="" crop="0,0,1,1" id="u48f7af65" class="ne-image">

:::color4
💡 提示

网关设备的凭证即Access Token值，在后面用模拟工具是需要按照修改后的凭证发送注册包，注册包内容即Access Token值。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672263088-d2816ca1-c9a9-4654-bb1c-eadc8e2ca363.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1456.5714285714287" title="" crop="0,0,1,1" id="ub2232658" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672331242-669aaff6-b282-4e74-868b-25c921e66f67.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1452.5714285714287" title="" crop="0,0,1,1" id="ua2b49f72" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672355400-22006a72-36ba-4126-80e1-2b57e5bb537c.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1461.7142857142858" title="" crop="0,0,1,1" id="u5dc6adc1" class="ne-image">

## 创建网关子设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672442803-ded12381-1569-4aac-991f-249d2f80dbe3.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1460" title="" crop="0,0,1,1" id="u3ae2e70b" class="ne-image">

:::color4
<font style="color:rgb(38, 38, 38);">💡</font><font style="color:rgb(38, 38, 38);"> 注意  
</font><font style="color:rgb(38, 38, 38);">网关子设备所在组织必须与网关设备一致。</font>

<font style="color:rgb(38, 38, 38);">创建网关子设备时所填的设备标识即modbus返回数据中的地址位，本次测试返回为01。</font>

:::

# 模拟TCP网关设备接入
## 使用工具连接平台
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672656623-4968f302-2c65-4121-99ed-b9d192176d78.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="666.8571428571429" title="" crop="0,0,1,1" id="u056f6d93" class="ne-image">

:::color4
💡 提示

如果无法连接平台请检查对应端口是否占用或检查网络问题。

:::

:::color4
💡 注意

<font style="color:rgb(38, 38, 38);">测试中使用服务器地址以及端口需要根据实际情况填写。</font>

<font style="color:rgb(38, 38, 38);">体验账号</font>

<font style="color:rgb(38, 38, 38);">服务器地址：demo.thingskit.com或47.101.144.59</font>

<font style="color:rgb(38, 38, 38);">端口号：8088</font>

:::

## 使用工具下发数据
首先要用测试工具发送注册包，然后使用注册工具发送测试数据：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672869057-1f7daa8e-20ac-4103-8ee5-0dd1e08a4eba.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="666.8571428571429" title="" crop="0,0,1,1" id="ub686fed3" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672856302-dd9d9581-d897-4b9b-94bd-a1b8d93a8e84.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="u14717226" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753672933693-90384109-3b7b-4ae2-a7c8-0ebe6bbcd680.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.2857142857142" title="" crop="0,0,1,1" id="u83f1e6cb" class="ne-image">

:::color4
💡 注意

使用测试工具发送注册包时需要选择ASCII码发送，且发送注册包内容要与凭证一致。

使用测试工具发送数据是需要选择HEX发送。

使用模拟工具发送数据至平台后，设备物模型显示的值就是0x4A的<font style="color:rgb(38, 38, 38);">十进制数74</font>。

:::
