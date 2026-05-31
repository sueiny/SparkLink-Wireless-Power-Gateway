---
title: Docker集群部署 V2.0.0
---

# 第1/10步：集群内服务器节点运行环境准备
:::warning
💡 提示

集群部署至少需要3个服务器节点。例如：3、5、7等。

:::

## 1/3：开启NTP(时间同步)服务
```shell
sudo apt install -y chrony --fix-missing
```

```shell
sudo chronyc makestep
```

```shell
chronyc tracking
```

<img src="https://cdn.nlark.com/yuque/0/2024/png/13018922/1731825263505-a202c458-084e-4a8e-9a3a-1119d8a23819.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_14%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="396.8" title="" crop="0,0,1,1" id="u55769a29" class="ne-image">

## 2/3：防火墙配置
:::warning
💡 提示

防火墙规则有上行(其它终端访问服务器)和下行(服务器访问网络资源)2种。

:::

端口清单如下：

[默认端口](https://yunteng.yuque.com/avshoi/v2xdocs/gqevy8bnlrr3sc3v)

## 3/3：Docker安装【如果已经安装，可略过】
**根据您的操作系统选择**：

[国产系统(Kylin 、openEuler ) Linux](https://yunteng.yuque.com/avshoi/v1xdocs/pc6mq85nntgusa1b)

[Ubuntu Linux](https://yunteng.yuque.com/avshoi/v1xdocs/ombfzh3udmssrpoo)

[CentOS Linux](https://yunteng.yuque.com/avshoi/v1xdocs/xl0p1wko115eixcn)

[Debian Linux](https://yunteng.yuque.com/avshoi/v1xdocs/bw1hge8ak1eu8coi)

# 第2/10步：获取部署包
[微服务部署包管理](https://yunteng.yuque.com/avshoi/v2xdocs/oghfpggynyeai8m6)

# 第3/10步：配置通用环境变量
:::color1
💡 提示

集群内所有节点保持一致的配置。

:::

[微服务修改通用环境变量](https://yunteng.yuque.com/avshoi/v2xdocs/gvsmq1rodmxety13)

# 第4/10步：配置负载均衡信息
:::color1
💡 提示

集群内所有节点保持一致的配置。

:::

[微服务修改负载均衡信息](https://yunteng.yuque.com/avshoi/v2xdocs/hue8351m1o47b537)



# 第5/10步：集群内服务器节点间同步部署包
:::warning
💡 提示

执行命令将部署包【_makeFile】内容同步到集群内的其它服务器节点。

:::

## 二选一：root用户
:::info
💡 提示

需要将文本【用于扩展微服务组件的服务器IP或域名】替换后执行命令。

例如：192.168.1.235

:::



```shell
scp -r /_makeFile root@用于扩展微服务组件的服务器IP或域名:/    #同步到其它服务器根目录                              
```

## 二选一：普通用户
:::info
💡 提示

需要将文本【用于扩展微服务组件的服务器IP或域名】替换后执行命令。

例如：192.168.1.235

:::

:::info
💡 提示

需要将文本【用户名】替换后执行命令。

例如：thingskit

:::

```shell
sudo scp -r /_makeFile 用户名@用于扩展微服务组件的服务器IP或域名:./    #同步到当前用户home目录                              
```

命令执行过程如图：

<img src="https://cdn.nlark.com/yuque/0/2023/png/13018922/1690949467090-12137c47-f06c-4214-9772-b902db2dd8ee.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_18%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="494.4" title="" crop="0,0,1,1" id="Vd4sS" class="ne-image">

# 第6/10步：修改服务节点的环境变量
:::color1
💡 提示

集群内每个服务器节点的配置都不一样，需要登录每个服务器节点，进行单独修改。

:::

## 1/5：获取license证书
:::warning
💡 提示

**部署版**需要为每个部署服务组件【tb-core】的服务器申请license证书。

:::

[部署版获取license证书](https://yunteng.yuque.com/avshoi/v2xdocs/ar8h7zdssg3cdzxk)

## 2/5：修改服务节点环境变量
[微服务修改服务节点的环境变量](https://yunteng.yuque.com/avshoi/v2xdocs/pg9aq67d17k6m8vl)

## 3/5：应用系统环境变量
```shell
cd /_makeFile
mv thingskit2.0.sh /etc/profile.d
```

## 4/5：应用系统环境变量
:::color1
💡 提示

如果source命令异常，可以重启系统使thingskit2.0.sh生效。

:::

```shell
source /etc/profile
```

## 5/5：校验环境变量有效性
:::color1
💡 提示

执行命令打印环境变量，存在相应输出说明环境变量有效。否则是要排查原因。

:::

```shell
echo $CLUSTER_NODE_ID
```

# 第7/10步：导入Docker的镜像文件
:::color1
💡 提示

集群内每个服务器节点都需要导入镜像。

:::

[Docker镜像导入](https://yunteng.yuque.com/avshoi/v2xdocs/wzht6mqzkr0zfw26)

# 第8/10步：安装微服务依赖软件
[微服务依赖软件部署](https://yunteng.yuque.com/avshoi/v2xdocs/gohp8pkbgdauon88)

# 第9/10步：部署微服务组件
平台的服务组件可实际情况部署到多台服务器

[微服务服务组件部署](https://yunteng.yuque.com/avshoi/v2xdocs/oe29vg86y1kggsp5)

# 第10/10步：测试ThingsKit物联网平台是否安装成功
## 创建租户
:::info
💡 提示

<font style="color:rgb(38, 38, 38);">设备的接入需要再超级管理员的账号登录后，创建租户-租户管理员并访问租户账号才能使用。</font>

:::

## 功能验证清单
[平台基础功能验收清单](https://yunteng.yuque.com/avshoi/v2xdocs/xmdy5eamsg2s0m4l)



# 扩展：启用安全协议SSL/TLS(Plus)
[启用安全协议SSL/TLS(Plus)](https://yunteng.yuque.com/avshoi/v2xdocs/qcwa9lm9uhgbw03i)
