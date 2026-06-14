---
title: 单机部署修改为redis缓存
---

:::info
💡提示

文档所有地址均为部署目录默认地址，如有不同请自行修改。

:::

# 1、准备工作
:::info
💡提示

当前单机部署平台正常运行，并确定服务器cpu架构。

:::

```shell
hostnamectl
```

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1776146865340-d6a24c39-1908-4eda-aea7-d795f9cff0ad.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_25%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="506.2857142857143" title="" crop="0,0,1,1" id="u526b12c4" class="ne-image">

:::info
💡提示

确定当前服务器部署cpu架构后，上传对应redis部署文件，可咨询相关人员获取。

:::

# 2、启动redis
## 2-1、上传redis部署文件
:::info
💡提示

上传redis.zip至平台部署目录。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1776223041466-5d217014-fb1d-448b-ae19-d48fb70fa1e3.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_29%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="582.2857142857143" title="" crop="0,0,1,1" id="uaf8a2efb" class="ne-image">

## 2-2、启用redis
:::info
💡提示

如果默认占用端口已经被占用则无法正常启动。

:::

```shell
cd /_makeFile/redis/
docker-compose up -d

docker ps		#查看当前启用容器
docker logs redis -f		#查看redis服务容器日志
```

启用后默认占用端口（可不对外开放）：

6379

16379

26379

# 3、修改并重启平台服务
## 3-1、修改monolith.env配置
:::info
💡提示

修改monolith.env内CACHE_TYPE。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1776149136381-dbbeca5f-9525-49b4-bdb4-b06acdc6b144.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_44%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="873.1428571428571" title="" crop="0,0,1,1" id="u7a5fa432" class="ne-image">

```shell
CACHE_TYPE=redis
REDIS_PASSWORD=${USER_PASSWORD}
REDIS_DB=0
REDIS_CONNECTION_TYPE=standalone   #redis部署模式:standalone/cluster
REDIS_NODES=${SERVER_HOST_IP}:6379    #redis集群(cluster)部署，需要被替换的模板变量05=192.168.0.65:6379,192.168.0.75:6379,192.168.0.85:6379
REDIS_HOST=${SERVER_HOST_IP}       #redis单体(standalone)部署 需要被替换的模板变量05=192.168.0.65
REDIS_PORT=${REDIS_SERVICE_PORT}   #redis单体(standalone)部署
```

## 3-2、重启平台
```shell
source /etc/profile				#没有重启服务器时  环境变量对单独会话生效  执行该命令避免环境变量错误
cd /_makeFile/thingskit/
docker-compose up -d

docker-compose logs monolith --tail=200 -f		#查看容器日志
```


