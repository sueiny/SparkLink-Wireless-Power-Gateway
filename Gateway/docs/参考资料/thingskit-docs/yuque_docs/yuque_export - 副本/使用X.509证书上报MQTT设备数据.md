---
title: 使用X.509证书上报MQTT设备数据
---

# 1、准备一个linux系统
:::info
💡 提示

可以使用centos7或unbuntu等常用linux系统。

需要用到openssl工具生成对应证书，如果不包含openssl请自行安装。且以下步骤均为centos7操作过程。

:::

:::info
💡 注意

以下步骤涉及修改平台服务器相关配置（默认未开启）和上传证书文件至指定服务器目录，上报时最好使用自己在linux系统下搭建的单机部署thingskit。测试版本2.0.2，单机部署。

:::

单机部署：

[Docker单体部署V2](https://yunteng.yuque.com/avshoi/v2xdocs/gcnbxr65zcgcnd87)

# 2、创建证书链
## 2-1、创建自签名CA证书
:::info
💡 提示

即创建根证书。

:::

```shell
openssl req -x509 -newkey rsa:4096 -keyout rootKey.pem -out rootCert.pem -sha256 -days 365 -nodes
```

:::info
💡 提示

后续Common Name选项中填写当前测试平台的域名或IP，其他选项默认即可，可以直接回车。

:::

:::info
💡 注意

执行此命令后，会在当前工作路径生成两个PEM后缀文件。

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1718939559695-ceca8832-6a95-4a3b-995a-e8981c3ebb93.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_22%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="472.1211848333425" title="" crop="0,0,1,1" id="u1d7911d8" class="ne-image">

:::

## 2-2、创建服务端证书
```shell
openssl req -new -newkey rsa:4096 -keyout serverKey.pem -out serverReq.csr -sha256 -nodes
```

:::info
💡 提示

<font style="color:rgb(38, 38, 38);">后续Common Name选项中填写当前测试平台的域名或IP，还有A challenge password需要输入至少4个字符的密码，其他选项默认即可，可以直接回车。</font>

:::

:::info
💡 提示

执行此命令后，会在当前工作路径下生成两个文件。

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1718939886728-b391fecc-7e77-4053-8d79-f9355672f5f1.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_23%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="498.1817893876862" title="" crop="0,0,1,1" id="u0fcb141e" class="ne-image">

:::

```shell
openssl x509 -req -in serverReq.csr -out serverCert.pem -CA rootCert.pem -CAkey rootKey.pem -days 365 -sha256 -CAcreateserial
```

:::info
💡 提示

执行此命令后，会在当前工作路径下生产两个个文件。

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1718940156033-c63b1b35-7323-4528-bcc3-8431cee132b6.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_25%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="521.2120910868737" title="" crop="0,0,1,1" id="ue81c05da" class="ne-image">

:::

## 2-3、创建客户端证书
```shell
openssl req -new -newkey rsa:4096 -keyout deviceKey.pem -out deviceReq.csr -sha256 -nodes
```

:::info
💡 提示

<font style="color:rgb(38, 38, 38);">后续Common Name选项中填写当前测试平台的域名或IP，还有A challenge password需要输入至少4个字符的密码，其他选项默认即可，可以直接回车。</font>

:::

:::info
💡提示

<font style="color:rgb(38, 38, 38);">执行此命令后，会在当前工作路径下生产两个个文件。</font>

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1718940456843-cb8d5702-44df-410b-8007-12393607c124.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_23%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="479.3939116857175" title="" crop="0,0,1,1" id="uca96528b" class="ne-image">

:::

```shell
openssl x509 -req -in deviceReq.csr -out deviceCert.pem -CA serverCert.pem -CAkey serverKey.pem -days 365 -sha256 -CAcreateserial
```

:::info
💡 提示

<font style="color:rgb(38, 38, 38);">执行此命令后，会在当前工作路径下生产两个个文件。</font>

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1718940513446-b4d2774d-55c4-417a-aed9-c5842aa04fb7.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_25%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="534.5454236495611" title="" crop="0,0,1,1" id="ua34543af" class="ne-image">

:::

:::info
💡 注意

到此步骤时，应该生成了以下文件：

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1718941299222-c6bd1530-564e-401d-a36b-2d75dd99bca7.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_23%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="433.06666666666666" title="" crop="0,0,1,1" id="u039ac307" class="ne-image">

:::

# 3、配置平台服务
:::info
💡提示

在进行平台mqtts配置时，需要将之前步骤生成的serverCert.pem、serverKey.pem复制到默认证书存放路径/_makeFile/_cert。

:::

## 3-1、修改monolith.env配置
```shell
MQTT_SSL_ENABLED=true
MQTT_SSL_CREDENTIALS_TYPE=PEM
MQTT_SSL_PEM_CERT=/_makeFile/_cert/serverCert.pem
MQTT_SSL_PEM_KEY=/_makeFile/_cert/serverKey.pem
MQTT_SSL_PEM_KEY_PASSWORD=secret
MQTT_SSL_SKIP_VALIDITY_CHECK_FOR_CLIENT_CERT=false
```

:::info
💡 提示

修改的配置文件默认路径为/_makeFile/monolith.env。其余配置均为默认。

默认端口号8883，需要注意防火墙或安全组是否开放该端口。

:::

## 3-2、重启平台服务
```shell
cd /_makeFile/thingskit
docker-compose up -d
```

# 4、创建设备
在平台上创建一个MQTT网关设备或直连设备且设备凭证选用X.509。

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1769762091675-12fd7e66-4d00-4d60-a1d4-be1d688d7f7e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_72%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1441.142857142857" title="" crop="0,0,1,1" id="u3e5d2e5b" class="ne-image">

:::info
💡 注意

RSA公钥用deviceCert.pem内容。

:::

# 5、使用MQTTX模拟客户端
:::info
💡 提示

按照以下方式连接测试平台地址。

:::

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1718941407373-1060597c-337c-4304-acdf-31a2565e14f2.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_59%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1097.0666666666666" title="" crop="0,0,1,1" id="u9463a88f" class="ne-image">

:::info
💡 注意

使用本文档方法为自签名CA证书，不能使用SSL安全的选项，也无需填入CA文件。

客户端证书使用deviceCert.pem。

客户端key文件使用deviceKey.pem。

设备上线成功后，数据上报过程与MQTT设备接入过程一致。

:::
