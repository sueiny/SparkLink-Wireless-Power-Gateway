---
title: MQTT Broker(EMQX)接入
---

当设备接入 EMQX 后，所采集的数据能够借助 EMQX 强大的消息处理能力，快速且稳定地转发至 ThingsKit 物联网平台。这一过程高效且精准，确保了数据从设备端到平台端的无缝传输。例如，在工业物联网场景中，各类传感器设备接入 EMQX 后，生产数据实时传输至 ThingsKit 平台，实现生产流程的实时监控与管理。使得设备数据能够顺畅地流向平台，为后续的数据处理、分析和应用提供有力支持。

# 安装及配置EMQX


```shell
services:
  emqx:   #账号密码【admin/public】
    image: emqx:5.8.3
    container_name: emqx
    restart: always
    privileged: true
    healthcheck:
      test: ["CMD", "/opt/emqx/bin/emqx_ctl", "status"]
      interval: 1m
      timeout: 25s
      retries: 10
    ports:
      - "18083:18083" # 管理后台端口
      - "1883:1883"  # mqtt端口
      - "18883:8883"  # mqtt TLS端口
      - "8083:8083"  # websocket端口
      - "18084:8084"  # websocket TLS端口
      - "18080:8080"  # HTTP API 端口
    environment:
      EMQX_LISTENER__TCP__EXTERNAL: 1883
```

# 模拟真实设备接入EMQX
## 创建设备凭证
<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742785987619-bbb799bc-7389-473b-a3e6-f77e48bb65fc.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_49%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1377.6" title="" crop="0,0,1,1" id="u84ef1640" class="ne-image">



<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742786090150-0095b4ee-d930-403c-b678-1292be746646.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_41%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1159.2" title="" crop="0,0,1,1" id="u27eb2a7b" class="ne-image">

## 模拟真实设备接入EMQX
<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742786056140-70078637-c543-4c72-b92a-e62368cea8fa.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_32%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="899.2" title="" crop="0,0,1,1" id="u35b32843" class="ne-image">

# EMQX对接ThingsKit平台
## 在ThingsKit物联网平台创建设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753867091425-75febc1d-7e56-41f3-8de0-928a0ce058e6.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1618.4127719097792" title="" crop="0,0,1,1" id="u1008389d" class="ne-image">

## 在EMQX平台创建规则链
<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742786176423-954efc5c-ecc0-4d2f-a29a-e17d8fa098ea.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_49%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1384.8" title="" crop="0,0,1,1" id="u524a68ec" class="ne-image">

### 创建过滤条件
:::color4
💡 提示

为规则创建的过滤器可以使用测试页面测试。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742786499246-b8482be6-700a-44f1-8c05-0cebf203625c.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_24%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="667.2" title="" crop="0,0,1,1" id="u1a60c4f7" class="ne-image">

### 添加动作输出
<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742786711710-81bdedda-40c6-49a7-a9f6-4eaf20e6529f.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_38%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1060.8" title="" crop="0,0,1,1" id="ucf204607" class="ne-image">

### 添加连接器
<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742786745286-5048e619-759b-4049-87c5-60f18ce65dcb.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_40%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1120.8" title="" crop="0,0,1,1" id="u1bd07bbd" class="ne-image">

:::color4
💡 提示

添加thingskit平台的MQTT设备服务的接入信息。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742787204234-1b955c7a-8677-42ce-9824-d2a9bb2e0bab.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_37%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1047.2" title="" crop="0,0,1,1" id="u69870cb5" class="ne-image">

:::color4
💡 提示

自定义推送给Thingskit平台的数据。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1742787296708-acc48c05-a4ac-4be0-9b6d-5cddef38f640.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_39%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1084.8" title="" crop="0,0,1,1" id="u8baf1aee" class="ne-image">

# 观察数据上报情况
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1753867538708-8f3008b3-37cf-4b01-8028-9fb7f74b6834.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u097ad909" class="ne-image">
