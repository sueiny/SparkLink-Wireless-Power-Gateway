---
title: Docker单体部署V2.0.0
---

:::color3
‼️‼️注意

V2.0.0版本之后部署请参考V2部署文档！

:::

# 端口清单
[默认端口](https://yunteng.yuque.com/avshoi/v1xdocs/ku7ippgf23xzy525)





# 第1/9步：Docker安装
**根据您的操作系统选择**：

[国产系统(Kylin 、openEuler ) Linux](https://yunteng.yuque.com/avshoi/v1xdocs/pc6mq85nntgusa1b)

[Ubuntu Linux](https://yunteng.yuque.com/avshoi/v1xdocs/ombfzh3udmssrpoo)

[CentOS Linux](https://yunteng.yuque.com/avshoi/v1xdocs/xl0p1wko115eixcn)

[Debian Linux](https://yunteng.yuque.com/avshoi/v1xdocs/bw1hge8ak1eu8coi)

# 第2/9步：获取部署包
## 已授权客户，请联系客服获取部署包
<img src="https://cdn.nlark.com/yuque/0/2023/webp/990998/1689663032189-2de21095-622c-46c3-b04e-24508c6b7fcf.webp?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_16%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="187" title="" crop="0,0,1,1" id="u3fab3415" class="ne-image">

扫码加微信

## 上传部署包并解压
:::color3
💡提示：

推荐将部署包(压缩文件)上传到部署服务器/目录。

例如：/

:::

:::warning
💡提示：

若执行unzip命令报错，则需要安装unzip的程序。

ubuntu：<font style="color:rgb(51, 51, 51);">apt-get install unzip</font>

centos：<font style="color:rgb(56, 58, 66);">yum install unzip</font>

:::

```shell
# 当前部署包版本请自行修改
cd /																						#将部署文件放到根目录
unzip /_makeFile_monolithV2.0.0.zip
mv v2.0.0/_makeFile_monolith/ _makeFile					#更改目录到默认目录
mv _workspace_monolithv2.0.0._non.zip /_makeFile/_workspace/
cd /_makeFile/_workspace/
unzip _workspace_monolithv2.0.0._non.zip				#此处仅为单机部署基础版文件
```

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1758507855061-1887723b-677a-4558-a999-bfa06fb3e9ce.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_57%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1269.2064068448992" title="" crop="0,0,1,1" id="u1136c9b7" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1758508157469-a858d29c-89f4-4d02-a80b-db378a176d5c.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_59%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1309.2064086614218" title="" crop="0,0,1,1" id="u8c5be6c0" class="ne-image">

```shell
cd /_makeFile/_workspace			#解压后文件夹为部署包版本
unzip web_ui.zip 
```

```shell
unzip scada.zip			#若没有购买该增值功能 则没有该文件
```

```shell
unzip data_view.zip 	#若没有购买该增值功能 则没有该文件
```

```shell
cd web_server
unzip data.zip 
```

:::color3
💡提示：

nodered以及gbt28181协议接入部署文件分别为nodered_extramod_monolith.zip、zlm_extramod.zip。

解压后请将对应文件放在/_makeFile/nodered/和/_makeFile/media/下。

:::

## 确认部署包结构和内容
:::info
💡 提示

解压后的部署包目录如下。_workspace中zip格式的压缩包需要解压。

:::

[单体部署包结构V2.0](https://yunteng.yuque.com/avshoi/v2xdocs/xmvq3zikff87gptx)

# 第3/9步：导入Docker的镜像文件
[Docker镜像导入](https://yunteng.yuque.com/avshoi/v2xdocs/wzht6mqzkr0zfw26)

# 第4/9步：获取license证书
:::info
💡 提示

源码版跳过第4步。

:::

[部署版获取license证书](https://yunteng.yuque.com/avshoi/v2xdocs/ar8h7zdssg3cdzxk)

# 第5/9步：配置环境变量
修改文件**<font style="background-color:#F4F5F5;">monolith.env</font>**(docker容器环境变量)中的环境变量。

:::warning
💡 提示

需要将环境变量LICENSE_SUBJECT的值【主体名称】修改为证书文件的名字。

:::

:::warning
‼️ 注意

需要将环境变量SERVER_HOST_DOMAIN的值【平台访问外网IP或域名】替换为第三方电脑可访问的IP或域名。

:::

:::warning
💡 提示

视频接入功能GBT28181默认是关闭的。启用该功能需要将配置文件中【GBT28181_ENABLED】的值改为【true】。

:::

```shell
cd /_makeFile

sed -i 's/平台访问外网IP或域名/XXX/g' monolith.env


#如果还可以找到内容【需要被替换的模板变量】说明配置文件编辑失败
cat monolith.env|grep 需要被替换的模板变量
```

操作示例：

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1758509518405-1322d345-de12-4185-932b-b479f42de83b.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_57%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1277.4603754737057" title="" crop="0,0,1,1" id="u2b3af30f" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2025/jpeg/36214471/1758509567776-7f9d025a-b6f3-452d-a654-4beb445bc291.jpeg?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_54%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1201.2698958231863" title="" crop="0,0,1,1" id="uaa9a4390" class="ne-image">

:::danger
⚠️ 警告

如果还可以找到内容【需要被替换的模板变量】说明配置文件编辑失败

:::

:::warning
💡注意

数据库密码需要手动设置。

:::

```json
echo $(openssl rand -base64 12 | tr -dc 'A-Za-z0-9' | head -c 11)		#获取12位随机密码以供替换
sed -i "s/默认密码/新密码/" ./monolith.env					#手动替换密码
```

:::warning
💡提示

如果没有成功设置数据库密码，启动时会报错

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1756435579243-e0873992-5e38-4ad7-bac5-64da0f4a69df.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_53%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1184.1270379018194" title="" crop="0,0,1,1" id="u07bad22a" class="ne-image">

:::

# 第6/9步：修改操作页面的配置信息
:::warning
💡提示

设备分布功能调用高德地图api，需要进行修改api key。

:::

[修改操作页面的配置信息](https://yunteng.yuque.com/avshoi/v2xdocs/hll8lqp6dznwl30t)

# 第7/9步：确认部署包内容
:::warning
💡 提示

升级部署时，需要先删除可执行文件xjar，对应的部署包thingsKit.xjar和xjar.go必须一对一匹配。

:::

```shell
/_makeFile/thingskit/_workspace/web_server/data/sql
rm -rf xjar

/_makeFile/thingskit/_workspace/web_server
rm -rf xjar
```

# 第8/9步：启动物联网平台
:::info
💡 提示

如果服务器开启了防火墙或有外网访问需求，则需要按照平台默认端口（如果自己调整了请按照调整后端口）开放入站规则。

:::

[默认端口](https://yunteng.yuque.com/avshoi/v2xdocs/gqevy8bnlrr3sc3v)

## 启动
```shell
cd /_makeFile/thingskit
sudo docker-compose	up -d
```

:::info
💡 提示

启动后如果无法访问平台服务可以参考下面的方式修改配置。

:::

<font style="color:rgb(38, 38, 38);">部署过程中查看日志报错找不到服务ID，应该如何解决？</font>

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1717639914778-e46f8052-734d-4621-ae44-17d3a88f2570.png?x-oss-process=image%2Fformat%2Cwebp%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_60%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2104" title="" crop="0,0,1,1" id="LsNGL" class="ne-image">

[安装部署问题](https://yunteng.yuque.com/avshoi/v2xdocs/gho03tgxganir968#JP7Mk)

启动服务是报错无法执行java-start.sh脚本，应该如何解决？

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1748240231408-2b774755-ce11-4d88-bcea-95b3aee3b884.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_57%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1276.8254548099512" title="" crop="0,0,1,1" id="ud78628db" class="ne-image">

[安装部署问题](https://yunteng.yuque.com/avshoi/v2xdocs/gho03tgxganir968#EhNzd)

## 查看启动日志
```shell
sudo docker-compose logs  --tail=200 -f    #查看管理界面日志
```

# 第9/9步：测试ThingsKit物联网平台功能
物联网平台、管理页面都成功部署后。我们就可以开始使用系统了。

:::info
💡 提示

访问地址：http://你的的IP或域名:9527

超级管理员账号:sysadmin

超级管理员密码:Sysadmin@123

租户管理员/客户默认密码：123456

:::

:::info
‼️ 注意

设备的接入需要再超级管理员的账号登录后，创建租户-租户管理员并访问租户账号才能使用。

:::

详情可参照：

[租户列表](https://yunteng.yuque.com/avshoi/v2xdocs/yor8wfhncl6wup4l)

## 平台是否安装成功验收清单
[平台基础功能验收清单](https://yunteng.yuque.com/avshoi/v2xdocs/xmdy5eamsg2s0m4l)

# <font style="color:rgb(38, 38, 38);">扩展：启用安全协议SSL/TLS(Plus)</font>
[启用安全协议SSL/TLS(Plus)](https://yunteng.yuque.com/avshoi/v2xdocs/qcwa9lm9uhgbw03i)

# 扩展：启动GBT28181协议的支撑软件ZLMediaKit
:::warning
‼️ 特别注意

该模块属于增值功能，不包含在基础功能内，部署文件需要额外获取。

:::

:::warning
‼️ 特别注意

视频接入功能GBT28181默认是关闭的。启用该功能需要将配置文件中【GBT28181_ENABLED】的值改为【true】。

:::

## 部署要求
[GBT28181网络要求](https://yunteng.yuque.com/avshoi/v2xdocs/shrwbhdzagwsfiqw)

:::warning
💡 注意

如果服务器使用网络无法使用外网可能会拉取不了对应容器镜像，可从平台服务人员获取，并导入镜像。

:::

```shell
cd /										#默认导入流媒体离线镜像到服务器根目录
docker load -i zlm.tar	#导入镜像
docker tag 534156187231 zlmediakit/zlmediakit:master	#修改名称
```

:::warning
💡 提示

还需要修改monolith.env中SERVER_HOST_IP为服务器ip。

:::

```shell
vi /_makeFile/monolith.env
```

```shell
cd /_makeFile/media
docker-compose	up -d

docker-compose logs  --tail=200 -f    #查看管理界面日志
```

# 扩展：启动nodered支持平台嵌入使用
:::warning
‼️特别注意

该模块属于增值功能，不包含在基础功能内，部署文件需要额外获取。

:::

:::info
💡 注意

nodered支持平台嵌入使用为1.5.2以及后续版本支持。

:::

[Node-RED部署Thingskit平台](https://yunteng.yuque.com/avshoi/v2xdocs/arhqxsxxct9c2gx1)
