---
title: 启用安全协议SSL/TLS(单体)
---

# 第1/3步：获取证书并上传到服务器
将获取到的证书上传到服务器目录**<font style="background-color:#FBDE28;">/_makeFile/_cert</font>**下。

<img src="https://cdn.nlark.com/yuque/0/2023/png/13018922/1698136024165-fb142d7d-9e45-47c5-a521-9f62a973e546.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_14%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="307.84" title="" crop="0,0,1,1" id="ub8ffdc5d" class="ne-image">

# 第2/3步：修改环境变量
:::warning
💡 提示

**<font style="color:#DF2A3F;">如果您需要开启HTTPS协议</font>**，需要将配置文件中的文本【com.thingskit.pem】和【com.thingskit.key】替换为你的证书文件名，并且确定外网访问地址（域名）以及端口（默认443）。

:::

可使用下面的命令替换环境变量。

```shell
cd /etc/profile.d/

##单机部署
sed -i 's/PLATFORM_SERVICE_DOMAIN=.*/PLATFORM_SERVICE_DOMAIN=域名或外网访问地址/g' thingskit2.0.sh
sed -i 's/PLATFORM_SERVICE_PORT=.*/PLATFORM_SERVICE_PORT=443/g' thingskit2.0.sh
sed -i 's/PROTOCOL_HTTP=http/PROTOCOL_HTTP=https/g' thingskit2.0.sh
sed -i 's/com.thingskit.pem/证书的公钥文件名/g' thingskit2.0.sh
sed -i 's/com.thingskit.key/证书的私文件名/g' thingskit2.0.sh

source /etc/profile						#使修改生效

#如果还可以找到内容【com.thingskit】说明配置文件编辑失败
cat thingskit2.0.sh|grep com.thingskit
```

# 第3/3步：重启软件
## 重新安装nginx
### 修改Nginx部署项目配置文件
:::info
💡 提示

如果web_ui配置文件中VITE_GLOB_STREAM_MEDIA_CONTENT_SECURITY_PROTOCOL参数并不存在则当前部署版本无法支持zlmhttps播放，请升级版本至少到1.4.0。

:::

```shell
cd /_makeFile/_workspace/web_ui
sed -i 's/"VITE_GLOB_SECURITY_POLICY":"false"/"VITE_GLOB_SECURITY_POLICY":"true"/g' _app.config.js
```



```shell
cd /_makeFile/_workspace/data_view
sed -i 's/"VITE_GLOB_CONTENT_SECURITY_POLICY":false/"VITE_GLOB_CONTENT_SECURITY_POLICY":true/g' _app.config.js
```



```shell
cd /_makeFile/_workspace/scada
sed -i 's/"VITE_GLOB_STREAM_MEDIA_CONTENT_SECURITY_PROTOCOL":false/"VITE_GLOB_STREAM_MEDIA_CONTENT_SECURITY_PROTOCOL":true/g' _app.config.js
```

### 修改Nginx配置文件
单机：

```shell
cd /_makeFile/thingskit/_image

sed -i '/#http access/s/^/#/' nginx.conf
sed -i '/#https access/s/^.//' nginx.conf		#放开443端口

sed -i 's/com.thingskit.pem/证书.pem/g' nginx.conf
sed -i 's/com.thingskit.key/证书.key/g' nginx.conf	#替换证书文件
```

PS：将这部分证书替换为自己的

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1744956095054-77f811b2-16bd-4b4a-835a-59b6d968e0c5.png?x-oss-process=image%2Fcrop%2Cx_50%2Cy_0%2Cw_1920%2Ch_614" width="1219" title="" crop="0.0254,0,1,1" id="uc130e4e1" class="ne-image">

### 重新安装Nginx
单机：

```shell
cd /_makeFile/thingskit/
docker-compose	restart

docker-compose logs  --tail=200 -f    #查看服务日志
```

微服务：

```shell
cd /_makeFile/webserver
docker-compose	restart

docker-compose logs  --tail=200 -f    #查看服务日志
```

## （可选）重新安装minio
微服务：

### 配置SSL证书
```shell
cd /_makeFile/storage
sed -i 's/#_makeFile//g' docker-compose.yml
sed -i 's/com.thingskit.pem/证书的公钥文件名/g' docker-compose.yml
sed -i 's/com.thingskit.key/证书的私文件名/g' docker-compose.yml

#如果还可以找到内容【com.thingskit】说明配置文件编辑失败
cat miscroservice.env|grep com.thingskit
```

### 重新安装minio
```shell
cd /_makeFile/storage
docker-compose	up -d

docker-compose logs  --tail=200 -f    #查看管理界面日志
```

## <font style="color:rgb(38, 38, 38);">（可选）ZLMediaKit流媒体部署使用SSL</font>
:::info
💡提示

如果平台使用https访问并需要使用流媒体服务，则需要对流媒体服务做对应配置。

:::

### 合并证书
```shell
cd /_makeFile/_cert
cat 证书名称.key 证书名称.pem >default.pem			#合并流媒体证书
```

### 修改docker-compose.yml
<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1768552352418-c5437868-f8a4-46fd-9022-9af146882f50.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_63%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1265.142857142857" title="" crop="0,0,1,1" id="u5d0d1e2f" class="ne-image">

:::color3
💡 注意

<font style="color:rgb(38, 38, 38);">如果开启了HTTPS协议后，需要使用流媒体服务则需将/_makeFile/media/docker-compose.yml中，视频实时流SSL的映射放开（默认注释）。</font>

<font style="color:rgb(38, 38, 38);">没有该配置可手动添加：- /_makeFile/_cert/default.pem:/opt/media/bin/default.pem</font>

:::

### <font style="color:rgb(38, 38, 38);">重启zlm容器</font>
```shell
cd /_makeFile/media
docker-compose up -d
```

## （可选）nodered部署使用SSL
:::info
💡 提示

如果平台访问方式改为了https，则nodered也需要调整配置以https方式访问。

:::

### 修改nodered相关配置并生效
```shell
cd /_makeFile								#切换至默认路径
sed -i 's|^#HTTPS_KEY|HTTPS_KEY|' monolith.env
sed -i 's|^#HTTPS_CERT|HTTPS_CERT|' monolith.env
docker restart monolith			#重启平台服务
```

### 修改nodered配置（2.0.0版本适用）
```shell
sed -i 's|^#HTTPS_KEY=.*|HTTPS_KEY=./_cert/证书的私钥文件名|' .env				#配置私钥
sed -i 's|^#HTTPS_CERT=.*|HTTPS_CERT=./_cert/证书的公钥文件名|' .env			#配置公钥

sed -i 's|FRAME_FRONT_END=http|FRAME_FRONT_END=https|' .env			#配置前端返回为https
sed -i 's|FRAME_BACK_END=http|FRAME_FRONT_END=https|' .env			#配置后端端返回为https
sed -i 's|:${FRAME_FRONT_END_PORT}||' .env												#更改返回地址
```

### 重新启动nodered（2.0.0版本适用）
```shell
cd /_makeFile/nodered/
docker-compose restart
```
