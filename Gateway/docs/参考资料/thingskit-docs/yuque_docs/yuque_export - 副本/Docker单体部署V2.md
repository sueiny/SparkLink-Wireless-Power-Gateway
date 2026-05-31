---
title: Docker单体部署V2
---

:::color3
‼️‼️特别提示

请勿将多个平台连接同一数据库！！！一个平台对应一个单独的数据库。

:::

:::color3
💡提示：

thingskit1.x版本部署包与当前部署文档结构差异很大，请对照版本参考文档。本文档基于x86架构服务器部署，如果需要对arm架构服务器部署，请先咨询相关人员。

:::

平台更新版本：

[平台更新版本指导](https://yunteng.yuque.com/avshoi/v2xdocs/oig334tixekh1twm)

# 端口清单
[默认端口V2](https://yunteng.yuque.com/avshoi/v2xdocs/nr3n8nzk56sk4nmi#LhzK9)

# 第1/**<font style="color:rgb(38, 38, 38);">8</font>**步：Docker安装
**根据您的操作系统选择**：

[国产系统(Kylin 、openEuler ) Linux](https://yunteng.yuque.com/avshoi/v1xdocs/pc6mq85nntgusa1b)

[Ubuntu Linux](https://yunteng.yuque.com/avshoi/v1xdocs/ombfzh3udmssrpoo)

[CentOS Linux](https://yunteng.yuque.com/avshoi/v1xdocs/xl0p1wko115eixcn)

[Debian Linux](https://yunteng.yuque.com/avshoi/v1xdocs/bw1hge8ak1eu8coi)

# 第2/**<font style="color:rgb(38, 38, 38);">8</font>**步：获取部署包
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
cd /																						#将部署文件放到根目录
unzip /_makeFile_monolithV2.x.zip
mv v2.x/_makeFile_monolith/ _makeFile					#更改目录到默认目录
mv _workspace_monolithv2.x._non.zip /_makeFile/_workspace/		#版本号按照部署包修改
cd /_makeFile/_workspace/
unzip _workspace_monolithv2.x._non.zip				#此处仅为单机部署基础版文件
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

gbt28181协议接入部署文件为zlm_extramod.zip。

解压后请将对应文件放在/_makeFile/media/下。

:::

## 确认部署包结构和内容
:::info
💡 提示

解压后的部署包目录如下。_workspace中zip格式的压缩包需要解压。

:::

[单体部署包结构V2.0](https://yunteng.yuque.com/avshoi/v2xdocs/xmvq3zikff87gptx)

# 第3/**<font style="color:rgb(38, 38, 38);">8</font>**步：导入Docker的镜像文件
:::info
💡提示

找到对应目录后进行镜像导入。

:::

```shell
cd /_makeFile/_images/				#切换到默认镜像存放路径
docker load -i thingskit_monolith2.0.1_x86.tar			#执行命令导入镜像  镜像名不唯一
docker image ls -a						#查看当前所有镜像
```

:::info
💡提示

导入镜像后还需要确认当前镜像名称是否与dokcer-compose.yml内一致。若不一致请修改。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1776763052699-7e5b6ad8-0884-4679-ad76-a0fdb05e81f7.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_33%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="664" title="" crop="0,0,1,1" id="u2c38f807" class="ne-image">

```shell
/_makeFile/thingskit/docker-compose.yml
```

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1776763110110-d0f47b06-8332-4f30-a46c-c1f00e1f05f2.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_46%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="929.1428571428571" title="" crop="0,0,1,1" id="u95b40e89" class="ne-image">

# 第4/**<font style="color:rgb(38, 38, 38);">8</font>**步：获取license证书
:::info
💡 提示

源码版跳过第4步。

:::

[部署版获取license证书](https://yunteng.yuque.com/avshoi/v2xdocs/ar8h7zdssg3cdzxk)

# 第5/**<font style="color:rgb(38, 38, 38);">8</font>**步：调整部署配置
修改文件**monolith.env**(docker容器环境变量)中的环境变量。

:::warning
💡 提示

视频接入功能GBT28181默认是关闭的。启用该功能需要将配置文件中【GBT28181_ENABLED】的值改为【true】。

:::

:::color3
💡提示：

首先要执行自动化处理脚本，获取当前服务器信息。按照基本提示设置，也可以直接回车选用默认值（初始化默认数据库密码需要等待8秒）。

:::

```shell
cd /_makeFile
chmod +x init_all.sh
./init_all.sh initPwd=true
```

:::color3
💡提示：

如果source命令异常，可以重启系统使thingskit2.0.sh生效。

:::

```shell
cd /_makeFile
mv thingskit2.0.sh /etc/profile.d
source /etc/profile
```

:::color3
💡注意：

执行脚本时，默认获取当前服务器内网ip作为访问平台的外网域名或ip，如果需要修改请重新输入！	

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1773038329693-50e7558e-21e9-495a-a199-a281f521e3a2.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_46%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="926.8571428571429" title="" crop="0,0,1,1" id="u1737405b" class="ne-image">

:::color3
💡提示：

如果在执行init_all.sh脚本时填错内容，可以在thingskit2.0.sh内进行修改。修改后需要重新执行source /etc/profile。

:::

# 第6/**<font style="color:rgb(38, 38, 38);">8</font>**步：确认部署包内容
:::warning
💡 提示

升级部署时，需要先删除可执行文件xjar，对应的部署包thingsKit.xjar和xjar.go必须一对一匹配。

:::

```shell
cd /_makeFile/thingskit/_workspace/web_server/data/sql
rm -rf xjar

cd /_makeFile/thingskit/_workspace/web_server
rm -rf xjar
```

# 第7/**<font style="color:rgb(38, 38, 38);">8</font>**步：启动物联网平台
:::info
💡 提示

如果服务器开启了防火墙或有外网访问需求，则需要按照平台默认端口（如果自己调整了请按照调整后端口）开放入站规则。

:::

[默认端口V2(单体)](https://yunteng.yuque.com/avshoi/v2xdocs/nr3n8nzk56sk4nmi)

改为jar方式启用：

[源码单机部署包调整](https://yunteng.yuque.com/avshoi/v2xdocs/mupga1p654ts6gxq)

## 启动
```shell
cd /_makeFile/thingskit
sudo docker-compose	up -d
```

:::info
💡 提示

启动后如果无法访问平台服务可以参考下面的方式修改配置。

:::

启动服务是报错无法执行java-start.sh脚本，应该如何解决？

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1748240231408-2b774755-ce11-4d88-bcea-95b3aee3b884.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_57%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1276.8254548099512" title="" crop="0,0,1,1" id="ud78628db" class="ne-image">

[安装部署问题](https://yunteng.yuque.com/avshoi/v2xdocs/gho03tgxganir968#EhNzd)

启动平台后报错显示大量的配置项无效，如图：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1774856888274-6972dad5-4005-4dbb-a5db-20707866dd1d.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_42%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="852" title="" crop="0,0,1,1" id="u8292291e" class="ne-image">

出现这个问题是因为新开了ssh远程会话导致当前环境变量没有生效，请在当前会话下执行命令后，在这个会话去启动平台服务：

```shell
source /etc/profile				#使当前会话环境变量生效
cd /_makeFile/thingskit
docker-compose up -d			#启动平台
```

<font style="color:rgb(38, 38, 38);">部署过程中查看日志报错找不到服务ID，应该如何解决？</font>

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1717639914778-e46f8052-734d-4621-ae44-17d3a88f2570.png?x-oss-process=image%2Fformat%2Cwebp%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_60%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2104" title="" crop="0,0,1,1" id="LsNGL" class="ne-image">

[安装部署问题](https://yunteng.yuque.com/avshoi/v2xdocs/gho03tgxganir968#JP7Mk)

## 查看启动日志
```shell
sudo docker-compose logs  --tail=200 -f    #查看管理界面日志
```

# 第8/8步：测试ThingsKit物联网平台功能
:::color3
💡注意

设备分布等功能调用地区需要获取高德地图api接口配置。

:::

配置地图流程：

[安装部署问题](https://yunteng.yuque.com/avshoi/v2xdocs/gho03tgxganir968#QDsmZ)

物联网平台、管理页面都成功部署后。我们就可以开始使用系统了。

:::info
💡 提示

访问地址：http://平台部署服务器IP或域名:9527

超级管理员账号:sysadmin

超级管理员密码:Sysadmin@123

租户管理员/客户默认密码：123456

:::

TB界面访问方式：

[安装部署问题](https://yunteng.yuque.com/avshoi/v2xdocs/gho03tgxganir968#VieYg)

:::info
‼️ 注意

设备的接入需要在超级管理员的账号登录后，创建租户-租户管理员并访问租户账号才能使用。

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

视频接入功能GBT28181默认是关闭的。启用该功能需要将配置文件monolith.env中【GBT28181_ENABLED】的值改为【true】。

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
docker image ls	-a				#查看所有docker镜像		如有空缺的镜像id执行docker tag命令修改tag

例如：
docker tag 534156187231 zlmediakit/zlmediakit:master
docker tag 5ff74fdb04df zlmediakit/zlmediakit:master
```

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1776322366516-05bc795a-6e8f-406a-863a-495d29ba3919.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_37%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="751.4285714285714" title="" crop="0,0,1,1" id="u123608b1" class="ne-image">

```shell
cd /_makeFile/media
docker-compose	up -d

docker-compose logs  --tail=200 -f    #查看管理界面日志
```


