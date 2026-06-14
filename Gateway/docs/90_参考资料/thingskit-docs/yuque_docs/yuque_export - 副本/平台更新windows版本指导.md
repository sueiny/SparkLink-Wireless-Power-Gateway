---
title: 平台更新windows版本指导
---

:::info
💡提示

文档中文件路径都使用默认路径，如果有更改，请自行调整。如nginx文件路径默认D:\nginx*，web_server文件路径默认D:\web_server。

:::

# 1、更新前准备
:::info
💡提示

更新前需要做些特殊处理，保证平台暂时不能访问（不会启动平台）且可以连接数据库。

:::

关闭平台后台启动的cmd窗口：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773826260945-ef10253c-f6ae-45a9-a2c4-c1ba1cd7ddab.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_42%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="760.5194522615116" title="" crop="0,0,1,1" id="u1c2d4e1b" class="ne-image">

关闭nginx进程（右键点击结束任务，两个都要关闭）：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773826733561-77277ccc-9882-4be0-8dae-04074b099b0e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_51%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="924.1558098177795" title="" crop="0,0,1,1" id="uf3929349" class="ne-image">

# 2、备份文件
## 2-1、备份前后端
:::info
💡提示

将nginx目录下的web_ui、data_view、scada文件夹，以及web_server下的data目录、thingsKit.xjar、xjar.go、xjar_agentable.go文件都备份一下。将文件复制都放在任意目录存放方面还原即可。

:::

## 2-2、<font style="color:rgb(38, 38, 38);">备份数据库</font>
:::info
💡提示

可以使用navicat数据库的备份功能创建备份。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773827449212-d083d4d2-8208-40e2-9e1b-5047125459a3.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_45%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="814.0259437799103" title="" crop="0,0,1,1" id="u5498756b" class="ne-image">

:::info
💡提示

还原数据库时请先删除原先数据库所有表，然后在点击还原备份。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773827632224-a0ec5ee9-a74a-4e03-b1d3-f12e286a5bb5.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_36%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="648.3116642229279" title="" crop="0,0,1,1" id="u803f29af" class="ne-image">

# <font style="color:rgb(38, 38, 38);">3、更换前后端</font>
## <font style="color:rgb(38, 38, 38);">3-1、更换前端</font>
:::info
💡提示

删除图中3个文件夹后将对应文件上传至nginx文件夹。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773827871786-eca9ff61-1784-4e64-a60d-a3ba61a92802.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="595.8441337048865" title="" crop="0,0,1,1" id="u7a554a60" class="ne-image">

## <font style="color:rgb(38, 38, 38);">3-2、更换后端</font>
:::info
💡提示

删除图中文件后将对应文件上传至web_server文件夹。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773827971624-6e5aa2fd-f59b-417f-a250-641b83bc37b3.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_37%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="675.8441307323952" title="" crop="0,0,1,1" id="u8fefacea" class="ne-image">

# <font style="color:rgb(38, 38, 38);">4、执行sql升级文件</font>
:::info
💡提示

升级必须确定当前版本后，按照一个版本一个版本的方式梯次执行sql更新数据库。如当前版本2.0.0，需要升级到2.1.0，则先执行thingsKit_2.0.0_2.0.1.sql，然后执行thingsKit_2.0.1_2.0.2.sql、thingsKit_2.0.2_2.1.0.sql。

:::

```yaml
D:\web_server\data\upgrade\thingskit
```

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773306975335-f669d601-2e89-467a-93b8-0879bf806079.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_48%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_48%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1669" title="" crop="0,0,1,1" id="RpPaK" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773307029548-a7c27a68-bf84-4a8e-b65f-7255c8bcfda4.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_48%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_48%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1691" title="" crop="0,0,1,1" id="j4fqY" class="ne-image">

# 5、**<font style="color:rgb(38, 38, 38);">重新启动平台</font>**
双击脚本启动平台后台：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773889165267-9617edca-6d27-419b-92e8-61d46321a321.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_35%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="643.1168592211417" title="" crop="0,0,1,1" id="udd7a0041" class="ne-image">

双击启动nginx：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773889194879-c974111d-7a38-4486-83a1-bf0dd10a5d40.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_35%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="629.6103662164974" title="" crop="0,0,1,1" id="uade60cbd" class="ne-image">
