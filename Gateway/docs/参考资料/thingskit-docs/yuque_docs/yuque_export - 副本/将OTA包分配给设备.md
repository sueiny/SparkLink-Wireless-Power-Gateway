---
title: 将OTA包分配给设备
---

# <img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1756890802903-98d89b69-d55f-433b-9eca-0aa877b3be57.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1918" title="" crop="0,0,1,1" id="u2e2b812d" class="ne-image">分配OTA升级固件包和软件包
首先，先新增产品，接着在OTA升级中添加固件或者软件并选择所属产品（新增的产品）。

:::color4
💡 提示

新增产品时，不可以分配OTA固件包和软件包。OTA升级包仓库对应的产品才可以在编辑时进行固件和软件分配。

:::

前提条件：新增[Default产品（默认）](https://yunteng.yuque.com/avshoi/v2xdocs/nqihhv66qdp735x5)和[创建直连或网关设备](https://yunteng.yuque.com/avshoi/v2xdocs/nse1xmr6t2gnt0w4)

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1756890370450-b44d88df-7443-4230-94f9-547b3459b3fa.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1920" title="" crop="0,0,1,1" id="u7c23b6e1" class="ne-image">

到产品页面点击编辑，选择该产品可使用的OTA包，选择OTA包即可。

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1756890634747-86c857c2-9dc9-4b8d-9ada-d85141d43cfe.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1920" title="" crop="0,0,1,1" id="Y19Jm" class="ne-image">

# 设备选择产品下OTA包
新增设备，选择分配了固件和软件的产品，设备进行编辑时，再选择所属的固件和软件，这样设备OTA升级包就分配好了。

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1756890807865-aabb68fd-400e-4b07-bb85-0962bcef814f.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1918" title="" crop="0,0,1,1" id="u4ce70744" class="ne-image">

:::color4
💡 提示

<font style="color:rgb(33, 37, 41);">例如：假设你的设备D1和D2具有产品C1：</font>

1. <font style="color:rgb(33, 37, 41);">如果将包F1分配给产品C1，设备D1和D2将更新为F1。</font>
2. <font style="color:rgb(33, 37, 41);">如果将包F2分配给设备D1，设备D1将更新为F2。</font>
3. <font style="color:rgb(33, 37, 41);">如果将包F3分配给产品C1将仅影响D2，因为它没有在设备级别分配特定的固件版本。因此D2将更新为F3而D1将继续使用F2，总的来说设备分配包的优先级大于产品分配包的优先级。</font>

:::


