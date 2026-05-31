---
title: TCP-Modbus-RTU实战指南
---

准备工作：

NetAssist.exe网络调试助手。创建TCP_modbus-rtu类型的产品和设备。

<img src="https://cdn.nlark.com/yuque/0/2024/png/36222522/1711965776676-6e32f954-ee1c-4a98-a492-03c504295c97.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_26%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="916" title="" crop="0,0,1,1" id="u22fe62b2" class="ne-image">

[NetAssist网络调试助手-软件工具-野人家园](https://www.cmsoft.cn/resource/102.html)

Modbus通信知识引导

[Modbus通信知识](https://yunteng.yuque.com/avshoi/hxd0ak/fclp7dt9iby5yo1t)

# TCP连续性地址数据实现
**连续性地址物模型json**：温度int类型（寄存地址1），湿度int类型（寄存地址2），氨气int类型（寄存地址3）。

TCP温湿度氨气json数据，可下载后直接在产品中导入该物模型json。

[TCP温湿度产品-model.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36222522/1750659262132-b2eeb79c-9f5b-4918-babb-aba5c78e1744.json)

#### 问询帧
| 地址码 | 功能码 | 起始地址 | 数据长度 | 校验码低位 | 校验码高位 |
| :---: | :---: | :---: | :---: | :---: | :---: |
| 0x01 | 0x03 | 0x00  0x00 | 0x00  0x03 | 0x05 | 0xCB |


:::color4
💡 提示

地址码：请求设备的地址码。功能码：表示查询保持寄存器。起始地址：代表查询寄存器起始地址为0000H。

数据长度：意思就是查询从0开始有几个寄存器。校验位：校验固定公式生成。

:::

#### 应答帧
| 地址码 | 功能码 | 字节数 | 温度值 | 湿度值 | 氨气 | 校验码低位 | 校验码高位 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 0x01 | 0x03 | 0x06 | 0x00 0x1F | 0x00 0x2D | 0x00 0x64 | 0x25 | 0x55 |


:::color4
💡 提示

地址码：请求设备的地址码。功能码：表示查询保存寄存器。字节数：代表后面数据的字节数（数据长度），由于一个寄存器两个字节，查询一个寄存器的数据，返回的数据长度就是两个字节，返回数据长度（单位字节）=查询寄存器格式*2。温度值湿度值等：寄存器数据值。校验位：校验固定公式生成。

:::

<font style="color:rgb(0,0,0);">温度：00 1F H(十六进制) = 31 =>如果缩放因子为10，则 温度= 3.1℃ （单位）。</font>

<font style="color:rgb(0,0,0);">湿度： 00 2D H(十六进制)= 45 => 如果缩放因子为10，则 湿度= 4.5%RH（单位）。</font>

准备hex16进制数据：01 03 06 00 1F 00 2D 00 64 25 55

操作流程：添加TCP产品-->添加该产品设备-->通过NetAssist.exe网络调试助手连接设备（连接时需要设备访问令牌发送进行验证）-->推送hex16进制数据-->设备物模型数据显示推送值。

## 创建TCP产品
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754276116742-7ce71411-b64c-4c03-ba21-f4b9d938c65c.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1618.4127719097792" title="" crop="0,0,1,1" id="u00d4d220" class="ne-image">

## 导入物模型属性
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754277407139-a8d9a1d7-c201-4f6c-b100-0c331385a37b.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1623.4921372198137" title="" crop="0,0,1,1" id="u610ebf2e" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754277567949-101d81b2-2c8d-45df-a048-08f6e5e80de3.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1623.4921372198137" title="" crop="0,0,1,1" id="u86cdbe71" class="ne-image"><img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754277629573-2ac8fc67-d6f5-4dad-9c3a-eada66e54cba.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1620.9524545647964" title="" crop="0,0,1,1" id="u2f0e24ba" class="ne-image"><img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754277654599-781921cb-6d0d-4a8e-a47c-f169373d42a2.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1622.222295892305" title="" crop="0,0,1,1" id="u5d917d19" class="ne-image">

## 创建TCP设备
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754278125704-e438ec37-3a13-41fb-bb0a-84a664a76e61.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u4375610d" class="ne-image">

## 确定设备凭证
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754278259226-984e23ca-7b14-4823-b725-2463ac7a5557.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u00da2f0e" class="ne-image">  
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754279286619-05aa100d-3693-46aa-a9fd-720f8854a324.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="u242dfa1b" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754279306853-16ed3dee-22e2-47d2-92d1-cf679db53148.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1621.5873752285506" title="" crop="0,0,1,1" id="u4eaee52f" class="ne-image">

## 模拟数据上报
<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754286878315-b6144392-fe76-4247-bfac-d39a76296418.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="740.9524146012994" title="" crop="0,0,1,1" id="u26b94939" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1754358694943-667043a5-b3ab-4d92-8673-6c782fe5eafb.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1620.9524545647964" title="" crop="0,0,1,1" id="u3fd6653d" class="ne-image">

# Modbus-Rtu不连续地址数据实现
**不连续性地址物模型json导入**：温度int类型（1个寄存器，寄存地址1），湿度int类型（1个寄存器，寄存地址2），一氧化碳单精度（2个寄存器，寄存器地址6），二氧化碳单精度（2个寄存器，寄存器地址8）。温湿度为连续地址，一氧化碳和二氧化碳为不连续地址。

[TCP不连续地址产品-model.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36214471/1754468954397-58afcfd4-c1bd-4a39-bb5d-c116c5704eea.json)

准备16进制数据：

温湿度为连续性地址：01 03 04 00 1F 00 2D 0B E8

一氧化碳和二氧化碳为不连续地址：01 03 08 42 0E 7A E1 42 83 33 33 F9 07

操作流程：添加TCP产品-->添加该产品设备-->通过NetAssist.exe网络调试助手连接设备-->先推送符合温湿度寄存器数的16进制数据-->再推送符合一氧化碳和二氧化碳寄存器数的16进制数据。

:::color4
💡 提示

因为modbus默认以连续性地址进制数据为准，如果寄存器地址不为连续，则需要推送不同的hex数据实现。

:::

<img src="https://cdn.nlark.com/yuque/0/2024/gif/36222522/1710138059329-6ae2ff77-4a00-4ac1-8d6b-b1a03d0bea01.gif" width="1910" title="" crop="0,0,1,1" id="ub21caf4f" class="ne-image">

## <font style="color:rgb(38, 38, 38);">创建TCP产品</font>
<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758851553461-b3ab8bf4-1267-40f0-aa1d-c41be1bf453d.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1920" title="" crop="0,0,1,1" id="ucd338867" class="ne-image">

## <font style="color:rgb(38, 38, 38);">导入物模型属性</font>
<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758851627073-e2209547-db48-43f5-9081-aea0ca9233cd.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1916" title="" crop="0,0,1,1" id="u2b72ff7d" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758851949279-6669a008-82f3-4547-b29e-7d13af00405c.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1918" title="" crop="0,0,1,1" id="u99025d5e" class="ne-image">

## <font style="color:rgb(38, 38, 38);">创建Modbus-Rtu设备</font>
<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758852513743-42bcdff9-5d05-4ecb-9f3e-574abf188af0.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1918" title="" crop="0,0,1,1" id="u5cbb0cf3" class="ne-image"><font style="color:rgb(38, 38, 38);">  
</font>**<font style="color:rgb(38, 38, 38);">确定设备凭证</font>**

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758852783639-993c5fb1-e842-4b3a-87dd-60b679daa9b9.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1920" title="" crop="0,0,1,1" id="u46df060c" class="ne-image">

## <font style="color:rgb(38, 38, 38);">模拟数据上报</font>
一氧化碳和二氧化碳物模型为不连续性地址。

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758853405674-0553757c-56a3-4f50-8568-42111b044a0f.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_80%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2812" title="" crop="0,0,1,1" id="u5d228097" class="ne-image">

# TCP位开关解析数据实现
## 比特位实现开关
**开关物模型json**：开关解析从0位开始，0-7为一个寄存器两个字节16位。测试的开关位分别为0，2，5，7，8。

16进制开关位数据：01 03 02 03 FF F8 F4。

[位解析产品-model.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36214471/1754468989014-79936bd8-b6fc-4a9b-913b-304e0808d6c0.json)

:::color4
💡 提示

开关位一般是一个寄存器->两个字节-十六位。下图为16进制转2进制数据，16位前面需补位0000，开关位效果为从右往左。

<img src="https://cdn.nlark.com/yuque/0/2024/png/36222522/1710146583794-693e8439-7f90-42a1-9b53-567baa46252e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_11%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="397" title="" crop="0,0,1,1" id="u07bd7a1c" class="ne-image">

:::

<font style="color:rgb(38, 38, 38);">设备物模型数据展示：</font>

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758854967217-dfea0b50-ad6e-4fde-83b7-44c9ec672a07.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1913" title="" crop="0,0,1,1" id="u4b952e90" class="ne-image">

## 正常线圈开关状态实现
开关物模型json：开关从0位开始解析，线圈为读取01，写入05，测试开关为5个，寄存器地址0-4连续，示例开关16进制数据为：01 01 01 15 90 47，开关状态为0001 0101，从右往左进行控制则为开，关，开，关，开。

[线圈开关产品-model.json](https://yunteng.yuque.com/attachments/yuque/0/2025/json/36222522/1758855234958-61b3856f-6b8f-4f36-bac8-85afcee71754.json)

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758854732783-28873831-e53e-4b95-ae87-9823b15248dd.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_19%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="745" title="" crop="0,0,1,1" id="u1c3c2477" class="ne-image">

设备物模型数据展示：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36222522/1758855288994-f539e529-761b-4405-8834-2c20923fb5df.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1914" title="" crop="0,0,1,1" id="ufbe47a96" class="ne-image">
