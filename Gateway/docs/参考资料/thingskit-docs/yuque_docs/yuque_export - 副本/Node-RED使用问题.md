---
title: Node-RED使用问题
---

# 如何对接不能修改订阅主题的mqtt协议设备？
:::warning
❓mqtt协议设备可以上报数据到平台，但设备订阅主题不可更改且不符合平台默认主题，thingskit平台目前不支持自定义发布主题，怎么搭配使用nodered实现设备对接？

:::

## 答案
:::info
💡 提示

平台自1.5.2版本后支持嵌入nodered功能平台直接使用，nodered功能节点中包含mqttbroker节点可以直接调用。如图中所示，在nodered中搭建mqttbroker，让设备直接访问。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1758702591872-a41d023d-e8cf-4219-a68e-6ebfc5573a70.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u206d4bc4" class="ne-image">

示例json（可直接导入到nodered，配置需要自行修改）：

[将mqttbroker上报数据改为json输出.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36214471/1761618070477-4c25d046-621a-409d-9807-4990e12f1d08.json)

:::info
‼️ 注意

1. nodered的mqttbroker必须使用未被占用的端口且通信设备可以访问。
2. 实现思路为首先将设备对接到nodered的mqttbroker上，再将对应json（若上报消息不为json请自行修改流程）解析出来通过mqtt out上报给平台设备，最后在用mqtt in节点订阅平台设备下发指令，并发送给<font style="color:rgb(38, 38, 38);">nodered的mqttbroker。在最后步骤中，发布给设备的主题就可以自行设置了。</font>
3. <font style="color:rgb(38, 38, 38);">如果下发给设备的命令json需要匹配设备的要求，则可以在“接受平台下发指令”的节点后添加函数节点自行修改：</font>

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1758702645673-9efebc8d-df6a-4201-9b0d-10b786ee9e0a.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u5acd06eb" class="ne-image">

4. <font style="color:rgb(38, 38, 38);">示例中配置可能需要修改，请按照实际情况修改。</font>
5. <font style="color:rgb(38, 38, 38);">nodered中的mqttbroker节点仅支持3.1、3.1.1版本协议。</font>

:::

---

# 如何在nodered中对不同tcp协议设备下发命令？
:::warning
❓ 当多设备对接到nodered的tcp in节点时，要对单个设备进行命令下发怎么实现（默认下发为广播）？

:::

## 答案
:::info
💡 提示

<font style="color:rgb(38, 38, 38);">平台自1.5.2版本后支持嵌入nodered功能平台直接使用，nodered功能节点中包含tcp对接节点可以直接调用。如图中所示，直接启用对应节点接入设备，并按照会话号区分下发设备。</font>

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1747127309915-ffdc7b45-9c92-4808-bc96-52cf5ea1eb69.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_62%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1390.4762536219757" title="" crop="0,0,1,1" id="ub413aa25" class="ne-image">

<font style="color:rgb(38, 38, 38);">示例json（可直接导入到nodered）：</font>

[tcp按照会话号下发示例.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36214471/1757302472219-cc7ec7db-5861-45d7-be21-33244f418de3.json)

:::info
‼️ 注意

1. 本流程使用的nodered支持的全局变量记录设备tcp连接对应会话号，且存储为json格式，key为设备注册包，value为会话号。
2. 示例中使用了两个注入节点作为示范，其中msg.sid作为注册包。
3. 当对接到平台时需要额外添加tcp/mqtt节点与平台设备建立连接，如图所示：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1747128253881-b231e7e0-45dc-43ba-82b3-b90828d51017.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_64%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1415.8730801721488" title="" crop="0,0,1,1" id="MMtW0" class="ne-image">

4. 该示例中上传数据和下发命令可能都涉及内容的转换或解析，请添加对应的函数节点做转换。

:::

---

# 如何在nodered中将tcp协议上报的json消息正常解析？
:::warning
❓向nodered的tcp in推送json输出的内容为字符串，怎样转换为json呢？

:::

## 答案
:::info
💡 提示

<font style="color:rgb(38, 38, 38);">平台自1.5.2版本后支持嵌入nodered功能平台直接使用，nodered功能节点中包含tcp对接节点可以直接调用。如图中所示，直接启用对应节点接入设备后，添加函数节点转换。</font>

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1747128893376-86ff1148-a3f3-423a-bd0e-5fc29f2abb73.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_34%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="768.2540031427354" title="" crop="0,0,1,1" id="u045387fe" class="ne-image">

[tcp协议json上报转换.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36214471/1757302472303-71879a39-9ee9-42e8-9ffc-902d8e3f2c86.json)

:::info
💡提示

也可以直接使用nodered的转换节点实现。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1772593103921-9d74afc4-7e54-42e2-af1d-f0353a9a47f0.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1462.857142857143" title="" crop="0,0,1,1" id="u9b373a6a" class="ne-image">

:::info
‼️ 注意

利用该方式转换为json后可直接对数据做具体调整，例如进行数据缩放、调整json结构复合平台mqtt协议上报格式，最后用mqtt out节点对接设备数据上报到平台。也可以使用数据上报节点。

:::

---

# 如何在nodered中将上报设备数据整合为网关+网关子设备json格式上报给平台？
:::warning
❓ 向平台上报的数据为单个网关接入多个传感器的上报数据，但格式并不符合网关+网关子设备格式以及主题，如何使用nodered实现设备数据按照网关+网关子设备格式上报给平台？

:::

## 答案
:::info
💡 提示

可以参考下面这个回答的流程基础上进行修改。

:::

[https://yunteng.yuque.com/avshoi/v2xdocs/ehflc7mq3ug3z02z#djUW4](#djUW4)

:::info
💡 提示

在此基础上添加解析节点将上报数据修改为网关+网关子设备格式通过“数据上报”到平台。

假设上报数据为{"deviceName":"test","temp":25.9}，其中test为网关子设备名称，25.9为实际上报数据点。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1761619517429-fe51d687-23d8-4e81-89fa-a8de6264cd38.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_63%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1270.857142857143" title="" crop="0,0,1,1" id="u5b4ab022" class="ne-image">

[整合为网关+网关子设备json格式上报.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36214471/1761619862938-29be0cd4-ffd5-48e6-a7aa-01d56bfddda8.json)

:::info
💡 注意

按照文档流程上报数据前需要创建对应的网关产品、网关子设备产品以及创建网关设备，才可以在调整“数据上报”节点中有设备和产品可以选择（名称自行定义）。

:::

示例：

数据上报：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1761619652646-ef691825-d3c6-448f-babd-b969d808ba15.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_62%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1252.5714285714287" title="" crop="0,0,1,1" id="u8f25703a" class="ne-image">

命令下发：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1761619670757-04214c1d-3ea8-4503-971b-dc2cc094eef1.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_63%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1266.2857142857142" title="" crop="0,0,1,1" id="u600b13e9" class="ne-image">

:::info
💡 提示

按照流程上报后，平台会自动创建按照deviceName键值的网关子设备。设备上报数据在网关子设备物模型内查看。不同的deviceName键值会创建不同的网关子设备，也可以自行手动创建，后续对应数据会更新其物模型属性。

:::

---

# 如何在nodered中调用平台的接口？
:::warning
❓ 当在平台中启用一个nodered实例时，如何直接调用平台的接口获取数据？

:::

## 答案
:::info
💡 注意

调用平台的接口可以参考对应的文档。

:::

[鉴权信息获取](https://yunteng.yuque.com/avshoi/v1xapi/xuhcbzbqucub6fvq)

参考流程（可导入）：

[nodered调用平台接口.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36214471/1763025848843-d0d0fb7e-af1e-48ac-8bba-f35de0b2c858.json)

:::info
💡 注意

在参考流程中需要修改对应的账号、密码完成token的获取，并且http请求的地址也需要按照你当前测试环境的真实地址（前端访问地址）来修改。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1763025999604-0fabefe1-c2b7-4f88-a8e5-d076854c3b36.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_63%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1269.7142857142858" title="" crop="0,0,1,1" id="uc5098660" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1763026038001-61c5a344-9afa-4de3-b755-b10ba48cf268.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_61%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1232.5714285714287" title="" crop="0,0,1,1" id="u53ebc470" class="ne-image">

:::info
💡 提示

实际请求结果可以在这个地方查看。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1763026189006-4a5c2c74-1ac0-41fd-9c8b-c12d8af78204.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_63%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1271.4285714285713" title="" crop="0,0,1,1" id="u030a25de" class="ne-image">

---

# 如何在nodered内进行备份（创造还原节点）？
## 答案
:::info
💡 提示

thingskit平台支持对nodered实例进行还原节点备份，备份后可以手动点击还原（系统会自动备份）。

:::

手动备份位置：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1764316326585-a2619645-ba1e-4d7d-bec2-4bda9c7b4201.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1326.2337169560376" title="" crop="0,0,1,1" id="uc4f7a9ce" class="ne-image">

手动还原位置：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1764316456890-e4e8f813-0e12-479c-b252-7b50fc8bab09.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1323.6363144551444" title="" crop="0,0,1,1" id="u679b1873" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1764316485747-44b352ab-e7db-47e7-be0f-3920874b8798.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1328.8311194569308" title="" crop="0,0,1,1" id="u2029a45b" class="ne-image">

---

# 导入流程后显示不存在节点，怎么解决？
## 答案
:::info
💡提示

在导入nodered流程后，如图显示不存在对应节点，是因为平台部署的nodered只包含默认nodered节点，如果需要使用其他的节点请联系相关工作人员，需要重新打包。

:::

示例：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1769674234544-163da7eb-374a-44eb-b7c4-7e1c8a157b7b.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_60%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1205.7142857142858" title="" crop="0,0,1,1" id="u01a16ba5" class="ne-image">

---

# 如何使用mqtt节点接入平台？
## 答案
:::info
💡提示

使用nodered内置mqtt节点连接平台时，可以按照设备接入的过程理解，需要首先在平台上配置产品-设备，然后配置节点的mqtt broker。

:::

创建mqtt产品：

[MQTT产品](https://yunteng.yuque.com/avshoi/v2xdocs/sicuwdofg7gfxel5)

创建mqtt直连设备:

[创建直连或网关设备](https://yunteng.yuque.com/avshoi/v2xdocs/nse1xmr6t2gnt0w4)

配置节点：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1770619769022-3b2f6d3a-680c-4e62-ac76-df337524e906.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_35%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="705.1428571428571" title="" crop="0,0,1,1" id="u783ffd3e" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1770619820933-fb47d4d1-9333-45b9-8cc0-129b527db884.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_40%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="811.4285714285714" title="" crop="0,0,1,1" id="u57c11c98" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1770619870602-ccfafb00-c252-4a6d-a834-c29b366cca77.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_40%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="798.2857142857143" title="" crop="0,0,1,1" id="u19c2392d" class="ne-image">

获取设备凭证：

[获取设备访问凭证](https://yunteng.yuque.com/avshoi/v2xdocs/sixb3ckuygfb7e25)

---

# 如何使用nodered解析时间戳（毫秒）？
## 答案
:::info
💡提示

可以在nodered处理时间戳转换为可以直接理解的时间格式。

:::

:::info
💡提示

nodered注入节点可以直接获取当前服务器时间戳（毫秒）。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1772592110464-c847429c-8b7e-4e06-8678-7644ccab44c2.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1458.857142857143" title="" crop="0,0,1,1" id="u23ec088b" class="ne-image">

[nodered解析时间戳（毫秒）.json](https://yunteng.yuque.com/attachments/yuque/0/2026/json/36214471/1772592341254-0600d9df-39d0-4cfc-8c20-9fcac2815923.json)

---

# 如何在nodered中使用http监听？
## 答案
:::info
💡提示

有时候为了调试http接口调用，可以在nodered内启用http监听进行调试， 也可以直接接收数据后传递给平台。

:::

首先确定nodered示例端口（如图中为21800）：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1775631322165-9bc3fb43-e657-41c1-9f07-7bbf728af415.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_71%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1430.857142857143" title="" crop="0,0,1,1" id="ub32b80eb" class="ne-image">

:::info
💡提示

调用节点http in，http response，debug组合一个简单的http监听流程。添加http response节点会在http请求后默认返回200状态码。

:::

nodered导入文件

[http监听.json](https://yunteng.yuque.com/attachments/yuque/0/2026/json/36214471/1775632694810-1e1ffbc6-f145-466d-b05d-a6114a494286.json)

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1775631595737-76746f80-442f-4fce-baf1-14337f9e1bb6.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_71%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1421.7142857142858" title="" crop="0,0,1,1" id="EYruF" class="ne-image">

:::info
💡提示

http in节点的内容可以自行编辑，如图

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1775632104006-c313630f-572e-491a-9e67-6d285960e17a.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_26%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="515.4285714285714" title="" crop="0,0,1,1" id="uc8781293" class="ne-image">

:::

按照该方式设置流程后 ，http工具请求的url如图（url由nodered所在平台地址+nodered实例端口和http inURL地址组合决定）：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1775632241875-4ad3ea51-b00f-4b8f-8963-f616e1855abf.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_49%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="974.2857142857143" title="" crop="0,0,1,1" id="u615c343b" class="ne-image">

上报后：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1775632350269-b7995912-78f9-4bf5-8391-1b28902fe3fd.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_62%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1234.2857142857142" title="" crop="0,0,1,1" id="uff4e90f9" class="ne-image">

---

# 如何在nodered将字符串转换为json？
## 答案
:::info
💡提示

有时候nodered接收的设备数据类型为字符串，但实际上数据格式为json，可以利用nodered自带节点json实现快速转换。可以看到图中已经将输如的字符串转换为Json输出了。也可以直接输入Json，默认会输出对应字符串。

:::

如图：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1775638887581-1883716a-8e18-4b33-927e-4dad29f785ba.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_61%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1231.4285714285713" title="" crop="0,0,1,1" id="u7a339241" class="ne-image">

nodered导入文件：

[字符串转换json示例.json](https://yunteng.yuque.com/attachments/yuque/0/2026/json/36214471/1775638981783-c279ce2e-3922-4266-ae6e-b116db1be0a9.json)

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1775638943106-ccd3112b-f1b8-4d98-a18c-8fbf8643ad05.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_61%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1231.4285714285713" title="" crop="0,0,1,1" id="ua4091c72" class="ne-image">

---

# 如何在nodered内将base64加密字符串转换为明文？
## 答案
:::info
💡提示

可以通过函数节点调用对应函数解析。

:::

<font style="color:rgb(38, 38, 38);">nodered导入文件：</font>

[base64加密字符串转换为明文.json](https://yunteng.yuque.com/attachments/yuque/0/2026/json/36214471/1776996471659-83113eb3-5358-4af9-be88-4e7a901cb927.json)

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1776996420660-8c45d64c-cc7d-4965-80c5-d6ec8fc2bf36.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_61%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1218.2857142857142" title="" crop="0,0,1,1" id="uae736c00" class="ne-image">

---
