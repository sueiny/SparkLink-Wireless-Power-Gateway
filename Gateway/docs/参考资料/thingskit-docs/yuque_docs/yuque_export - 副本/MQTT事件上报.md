---
title: MQTT事件上报
---

:::color4
💡注意：

事件的推送只支持**MQTT协议**的设备，其他协议暂不支持

事件主题：

        * **v1/devices/event/${deviceId}/${identifier}** 
        * **v1/devices/event/${deviceName}/${identifier}**

**${deviceId}：**设备详情页面的设备ID，例如：e1cf7400-7d6a-11f0-b123-dfb32e5aa109

**${identifier}：**产品物模型事件里面定义的标识符，例如：alarm_event

**${deviceName}：**设备的**设备名称**，千万不能复制成了设备的别名‼️‼️‼️‼️‼️

:::

# 前置条件
产品的物模型事件已创建，具体操作请通过[创建事件](https://yunteng.yuque.com/avshoi/v2xdocs/eue7yv9tcxrfrnqh)进行查看。产品对应的设备已经正常创建，具体操作请通过进行查看[创建直连或网关设备](https://yunteng.yuque.com/avshoi/v2xdocs/nse1xmr6t2gnt0w4)进行查看。

# 准备工作
:::color4
💡注意：

在使用设备名称时，注意设备是否有别名。最好通过编辑查看下设备的名称

:::

## 获取设备ID或设备名称
### 获取设备ID
<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755675336007-9eb22a7c-fcaf-4f02-8265-7b19687222ba.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2560" title="" crop="0,0,1,1" id="u0a2309f7" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755675339874-95372b84-8b27-4a56-92fc-9a55e9866111.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2560" title="" crop="0,0,1,1" id="ub413ca28" class="ne-image">

### 获取设备名称
<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755675351223-ddca185e-2c7e-4c9f-ad46-bdc58d71989f.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2555" title="" crop="0,0,1,1" id="u32c4ee2f" class="ne-image">

## 获取产品标识符
<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755675360146-11911f60-3cc4-4eee-bcf5-ef223b405745.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1160" title="" crop="0,0,1,1" id="u6d77040a" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755675369040-e3f1b954-abae-436a-b787-9063fdfa2213.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2560" title="" crop="0,0,1,1" id="u6c688535" class="ne-image">

# 事件上报
以下是MQTT直连设备上报的示例，如果网关要上报网关子设备的事件，把ID或名称换为子设备的名称即可

<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755173201042-5e042d57-9ff2-4289-b07e-532f6d73035d.png?x-oss-process=image%2Fformat%2Cwebp%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_29%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1011" title="" crop="0,0,1,1" id="lKiTA" class="ne-image">

## 设备ID上报
<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755675923787-084f7f6f-50ae-4196-9ff4-a8286034e14e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_32%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1116" title="" crop="0,0,1,1" id="u8d527529" class="ne-image">

查看上报结果，设备**详情 **> **事件管理** > **输出参数**

<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755675932902-f9680abd-77b2-477e-9aef-0f07e8699e3f.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2560" title="" crop="0,0,1,1" id="ud9429d34" class="ne-image">

## 设备名称上报
<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755676410732-1abcf368-485f-4c7f-b90d-77f903957384.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_32%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1116" title="" crop="0,0,1,1" id="uf917db13" class="ne-image">

查看上报结果，设备**详情 **> **事件管理** > **输出参数**

<img src="https://cdn.nlark.com/yuque/0/2025/png/38476533/1755676416889-34adfb11-8ec9-45fe-bfb5-5f25aa77cc10.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2560" title="" crop="0,0,1,1" id="ua5c5f0c5" class="ne-image">
