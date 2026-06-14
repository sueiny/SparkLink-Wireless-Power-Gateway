---
title: 模拟直连设备MQTT接入
---

# 准备工作
## <font style="color:rgb(38, 38, 38);">MQTTX设备模拟工具下载</font>
<img src="https://cdn.nlark.com/yuque/0/2023/png/990998/1688520722055-1ebbd579-5897-4b34-aee9-d18fadf639ac.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_29%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1009" title="" crop="0,0,1,1" id="vt35b" class="ne-image" style="color: rgb(38, 38, 38); font-size: 12px"><font style="color:rgb(38, 38, 38);">  
</font><font style="color:rgb(38, 38, 38);">MQTTX是由EMQ开发的一款开源跨平台MQTT 5.0桌面客户端，它兼容macOS，Linux以及Windows系统。  
</font><font style="color:rgb(38, 38, 38);">MQTTX的用户界面UI采用聊天式设计，使得操作逻辑更加简明直观。它支持用户快速创建和保存多个MQTT连接，便于测试MQTT/MQTTS连接，以及MQTT消息的订阅和发布。</font>

[MQTTX 下载](https://mqttx.app/zh/downloads)

# 平台创建模拟设备
## 创建直连产品
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431320003-4530313b-4f6c-468a-95bb-58f5a679ae23.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1441.7142857142858" title="" crop="0,0,1,1" id="u6bf541a0" class="ne-image">

## 创建产品物模型
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431356986-3531c78e-ccc3-4f7f-925a-6cbbc0c455bc.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1447.4285714285713" title="" crop="0,0,1,1" id="u6beedd03" class="ne-image">

:::color4
💡 提示

创建物模型按键需要点击“编辑物模型切换”。  

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431454903-7b74969c-5689-489e-9846-14bdc7f1bb45.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="ue0a2870e" class="ne-image">

:::color4
💡 提示

测试产品物模型可自定义，本次创建物模型为范例。

:::

## 创建直连设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431590676-2c8139e3-4d2c-44d3-8d63-4dda4599c707.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.2857142857142" title="" crop="0,0,1,1" id="ue3b04254" class="ne-image">

## 确认设备凭证
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431630746-70d706ee-3a7b-47d8-95fc-9e1b445da7c9.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1461.7142857142858" title="" crop="0,0,1,1" id="uc743f9a4" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431656469-77e4106b-ba7e-4c4c-84c0-0f1976af77e4.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1461.142857142857" title="" crop="0,0,1,1" id="u9344e366" class="ne-image">

# <font style="color:rgb(38, 38, 38);">模拟MQTT直连设备接入</font>
## 使用工具连接平台
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431776168-e497f32c-2f3b-47f5-88c0-778db09dfa5a.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_34%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="686.8571428571429" title="" crop="0,0,1,1" id="u1e320400" class="ne-image">

:::color4
💡 提示

如果工具显示连接失败请检测凭证是否一致。用户名则为设备详情中访问令牌。

凭据类型分为：Access Token，X.509，MQTT Basic

:::

连接后在平台查看设备上线情况：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431797762-d1047087-d043-4960-8ac9-f691c25fab17.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1461.142857142857" title="" crop="0,0,1,1" id="udf8b8761" class="ne-image">

## 使用工具测试数据
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431908807-80e6d685-8329-41ae-94b7-7a9cc631987a.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="665.7142857142857" title="" crop="0,0,1,1" id="uccbfdcfc" class="ne-image">

:::color4
💡 提示

测试数据必须是json格式。

:::

```plain
v1/devices/me/telemetry
```

:::color4
💡 提示

直连设备的发布主题可以修改。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431850790-ec167759-f032-4101-a964-1de6439214ee.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1455.4285714285713" title="" crop="0,0,1,1" id="uc3c05812" class="ne-image">

```json
{"test":2111111}
```

:::color4
💡 注意

传递数据的键必须符合物模型标识符，不符合则无法显示在平台上。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753431886712-d69865cd-e5c8-43e4-a17b-2639d1a94310.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="ufe89127f" class="ne-image">
