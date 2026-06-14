---
title: 部署版获取license证书
---

# 提供授权码
:::warning
💡 提示

向thingskit提供部署服务器带有ip的MAC地址，以生成对应的license证书。

:::

```shell
ip a                            #查看网卡信息
```

将下图中红框内的MAC信息提供为thingskit。

<img src="https://cdn.nlark.com/yuque/0/2024/png/13018922/1714987973995-6dda6287-93b8-4286-b524-b83d6b9eb787.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_24%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="676" title="" crop="0,0,1,1" id="wgkOR" class="ne-image">

:::warning
💡 提示

1.4.1版本前需要配置主体名称，1.4.1版本后不需要配置。

:::

# 上传证书
## 单体
:::color3
💡 提示

从技术人员获取对应license证书后，需要将license证书和公钥存放至/_makeFile/_cert/。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1774940263200-5eccbc27-cf4d-4319-8d15-da0f2482ca48.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_47%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="942.8571428571429" title="" crop="0,0,1,1" id="ue9eb1697" class="ne-image">

## 微服务
:::color3
💡 提示

从技术人员获取对应license证书后，需要将license证书和公钥存放至/_makeFile/_cert/。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1774940280461-9ff63679-f7ab-47d0-86c5-f71344d6c659.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_48%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="955.4285714285714" title="" crop="0,0,1,1" id="u0d1d0843" class="ne-image">
