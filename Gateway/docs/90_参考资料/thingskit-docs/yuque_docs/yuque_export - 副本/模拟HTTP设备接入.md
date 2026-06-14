---
title: 模拟HTTP设备接入
---

# 准备工作
## POSTMAN设备模拟工具下载
<img src="https://cdn.nlark.com/yuque/0/2023/png/36214471/1689582309538-72a7d31c-4ac8-48ce-858d-4d2dea16869b.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1281.3333333333333" title="" crop="0,0,1,1" id="u4eb97ce9" class="ne-image">

<font style="color:rgb(77, 77, 77);">POSTMAN是一款支持HTTP协议的接口调试与测试工具，其主要特点就是功能强大，使用简单且易用性好 。无论是开发人员进行接口调试，还是测试人员做接口测试，POSTMAN都是首选工具之一 。</font>

[Postman](https://www.postman.com/)

# 平台创建虚拟设备
## 创建直连测试产品
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753756024901-c8c4690c-6422-48f2-b737-a408561dc265.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1622.8572165560593" title="" crop="0,0,1,1" id="uf896a4f7" class="ne-image">

## 创建物模型
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753756075791-9452e181-704e-4a86-92d2-e0ded9380a8b.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u40876964" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753756176188-eab80825-70ed-4b4f-8636-87181dc1f589.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1624.7619785473223" title="" crop="0,0,1,1" id="u02a5b280" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">测试产品物模型可自定义，本次创建物模型为范例。</font>

:::

## 创建直连测试设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753756569639-1f5a6ad6-e397-4f0e-90a8-26c05ed6e6a2.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1620.317533901042" title="" crop="0,0,1,1" id="ucca3efdf" class="ne-image">

## 确认设备凭证
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753756962290-b3d66c17-1253-40fc-a98c-51701e9f9579.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1607.6191206259555" title="" crop="0,0,1,1" id="uc1702b2a" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753757040510-6c90a905-b65d-4eba-b966-fb4aab868400.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1620.317533901042" title="" crop="0,0,1,1" id="u6fd98717" class="ne-image">

# 模拟HTTP设备接入
## 填写工具参数
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753757684665-ff76f240-5a5f-4008-b5f0-1aa613613cf8.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1622.222295892305" title="" crop="0,0,1,1" id="u50f2157a" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">如果工具显示连接失败请检测凭证是否一致。	</font>

:::

:::color4
💡 注意

<font style="color:rgb(38, 38, 38);">测试中使用服务器地址以及端口需要根据实际情况填写。</font>

:::

## 发送数据
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753757761746-3f13f64b-8f20-403c-a1b4-7c8cb44ddadb.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_54%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1208.2540231244839" title="" crop="0,0,1,1" id="ua2a4a007" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(38, 38, 38);">测试数据必须是json格式。</font>

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753757791682-6aed7589-0164-4d70-8c0e-f5e259b966be.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1624.127057883568" title="" crop="0,0,1,1" id="u271b079c" class="ne-image">

```http
http://192.168.0.216:8080/api/v1/868913052045635/telemetry
```

```json
{
    "test": 666
}
```

:::color4
💡 注意

http请求的端口号随后台端口号改变，不一定是8080。平台默认为8080

:::
