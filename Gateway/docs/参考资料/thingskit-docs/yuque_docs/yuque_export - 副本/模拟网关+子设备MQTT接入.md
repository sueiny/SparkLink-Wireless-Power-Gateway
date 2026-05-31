---
title: 模拟网关+子设备MQTT接入
---

# 准备工作
## **<font style="color:rgb(38, 38, 38);">MQTTX设备模拟工具下载</font>**
<img src="https://cdn.nlark.com/yuque/0/2023/png/990998/1688520722055-1ebbd579-5897-4b34-aee9-d18fadf639ac.png?x-oss-process=image%2Fcrop%2Cx_0%2Cy_4%2Cw_1009%2Ch_737" width="749" title="" crop="0,0.0055,1,1" id="ac8oh" class="ne-image" style="color: rgb(38, 38, 38); font-size: 12px"><font style="color:rgb(38, 38, 38);">  
</font><font style="color:rgb(38, 38, 38);">MQTTX是由EMQ开发的一款开源跨平台MQTT 5.0桌面客户端，它兼容macOS，Linux以及Windows系统。 MQTTX的用户界面UI采用聊天式设计，使得操作逻辑更加简明直观。它支持用户快速创建和保存多个MQTT连接，便于测试MQTT/MQTTS连接，以及MQTT消息的订阅和发布。</font>

[MQTTX 下载](https://mqttx.app/zh/downloads)

# <font style="color:rgb(38, 38, 38);">平台创建模拟设备</font>
## 创建网关产品
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753432517211-e52c5cef-3631-49fa-a8bf-ed35c0cdf2bc.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1458.857142857143" title="" crop="0,0,1,1" id="u1dff7f23" class="ne-image">

## <font style="color:rgb(38, 38, 38);">创建网关子设备产品</font>
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753432597060-39a10044-53b5-4cc8-ae53-bfb6decaa63a.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1461.7142857142858" title="" crop="0,0,1,1" id="u9fa02177" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753432625649-abe114c7-c15e-4163-9710-8cc649904d2c.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1443.4285714285713" title="" crop="0,0,1,1" id="u44bb324c" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753432734675-06bb5e76-6bb4-4891-9402-4b9e9ec54fee.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.2857142857142" title="" crop="0,0,1,1" id="u9c34cd57" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">测试产品物模型可自定义，本次创建物模型为范例。</font>

:::

## 创建网关设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753432843607-4e0bcfa6-772a-4c80-b8aa-8b8150a4af1e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="u4accb92f" class="ne-image">

## 创建网关子设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753432919764-ef8e6520-1846-4868-a063-967f73f95941.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="u4612afa7" class="ne-image">

## <font style="color:rgb(38, 38, 38);">确认设备凭证</font>
:::color4
💡 提示

注意在接入网关+网关子设备时，我们需要确定的设备凭证为网关设备。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433076857-286b21e1-a0a5-44a7-822b-c58d4b2a66f0.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="ub9706221" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433150116-01cc9b72-ef70-4e76-8157-d392c177e3e8.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1454.857142857143" title="" crop="0,0,1,1" id="u61319d98" class="ne-image">

# 模拟MQTT网关设备接入
## 使用工具连接平台
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753342100171-d5a50e5a-0161-46e8-9600-9bc54ea65f0d.png?x-oss-process=image%2Fcrop%2Cx_0%2Cy_0%2Cw_1774%2Ch_1289" width="1014" title="" crop="0,0,1,0.9909" id="ud5f8c893" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">如果工具显示连接失败请检测凭证是否一致。	</font>

:::

:::color4
💡 注意

<font style="color:rgb(38, 38, 38);">测试中使用服务器地址以及端口需要根据实际情况填写。</font>

<font style="color:rgb(38, 38, 38);">体验账号</font>

<font style="color:rgb(38, 38, 38);">服务器地址：demo.thingskit.com或101.133.234.90</font>

<font style="color:rgb(38, 38, 38);">端口号：1883</font>

:::

<font style="color:rgb(38, 38, 38);">连接后在平台查看设备上线情况：</font>

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433237258-fbc6d9f7-c916-4bf8-9ba0-94ee07e8c8c2.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1457.142857142857" title="" crop="0,0,1,1" id="u6088ca0f" class="ne-image">

## 使用工具下发测试数据
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753342440727-7d9b36b3-8574-4072-85cc-d539e9bf7e80.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="656" title="" crop="0,0,1,1" id="u7057a889" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">测试数据必须是json格式。</font>

:::

```plain
v1/gateway/telemetry
```

```json
{
  "mqtt网关子设备":[
    {
      "test":666
    }
  ]
}
```

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753433284271-3d378ed3-71f5-4d2c-83b8-53b1c491efb1.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="u27e3ab73" class="ne-image">
