---
title: Node-RED部署Thingskit平台
---

:::info
💡 提示

本文档适应于如何在2.0版本部署nodered容器适配平台。部署所需文件请咨询相关人员获取。

:::

:::warning
‼️ 特别注意

本文档没有涉及对nodered服务启动时端口的修改，如果当前环境有修改thingskit的前后端口以及数据库访问信息等等，非默认配置启动则可能出现容器无法启动或无法访问nodered的情况。请咨询相关人员解决处理。

:::

nodered默认端口：

[默认端口](https://yunteng.yuque.com/avshoi/v2xdocs/gqevy8bnlrr3sc3v)

nodered配置说明（http）(可以手动配置也可以根据变量自动配置)：

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1745304020951-4ae45f6a-8a61-4d73-9888-63eb9740bdb8.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1215.8730710895359" title="" crop="0,0,1,1" id="u3782b7f3" class="ne-image">

# 准备工作
:::info
💡 提示

需要将nodered镜像包导入到部署环境docker镜像中。

:::

```shell
cd /_makeFile/_image
docker load -i nodered.tar
```

:::info
💡 提示

需要将nr.zip在/_makeFile/_workspace下解压。

:::

```shell
cd /_makeFile/_workspace/
unzip nr.zip
```

# 具体操作
## 修改环境变量（访问地址可参考 已经更改为变量应用环境环境变量配置文件）
### 低版本升级操作
:::info
💡 注意

如果是低于1.5.2版本升级需要使用nodered，则需要在monolith.env或是创建miscroservice_nodered.env自行添加配置项，并更新nginx配置。

:::

环境变量需要添加的配置：

```shell
#启用nodered
NODERED_ENABLED=false
```

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1743152558836-648e0202-c4fc-4add-bdef-4bfe3e63a250.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1624.7619785473223" title="" crop="0,0,1,1" id="ue5cc315c" class="ne-image">

nginx.conf或nginx.template需要添加的内容：

```shell
location /red/ {
    proxy_pass  http://localhost:3000/;
    proxy_set_header Host $host;
    proxy_set_header X-Real-IP $remote_addr;
    proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto $scheme;
    }
```

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1743152768364-ca88c46e-0832-4ebd-91e7-bc266a0574ba.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_73%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1625.3968992110767" title="" crop="0,0,1,1" id="uad2b7186" class="ne-image">

### 修改服务相关配置
单机部署：

```shell
cd /_makeFile
sed -i 's/NODERED_ENABLED=false/NODERED_ENABLED=true/g' monolith.env
cd /_makeFile/_workspace/nr
#服务器访问地址填写服务器外网ip 域名如果没有就用内网ip
sed -i 's/HOST=localhost/HOST=服务器访问地址/g' .env
sed -i 's/IP=localhost/IP=服务器内网ip/g' .env
```

微服务部署：

```shell
cd /_makeFile
sed -i 's/NODERED_ENABLED=false/NODERED_ENABLED=true/g' miscroservice_nodered.env
cd /_makeFile/_workspace/nr
#服务器访问地址填写服务器外网ip 域名如果没有就用内网ip
sed -i 's/HOST=localhost/HOST=服务器访问地址/g' .env
sed -i 's/IP=localhost/IP=服务器内网ip/g' .env
#修改nginx配置文件
sed -i 's/localhost:3000/服务器内网ip:3000/g' nginx.template
docker restart nginx						#重启服务使配置生效
```

### （可选）修改nodered实例启动配置
:::info
💡 提示

平台启动nodered服务有默认配置，需要占用11800-12000一共201个实例的端口，如果有端口冲突，或者需要减少个数，则可以在/_makeFile/_workspace/nr/.env内修改。

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1745303647784-606c54fe-855d-4897-90be-75f935655786.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_45%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1006.349252050608" title="" crop="0,0,1,1" id="u580e8606" class="ne-image">

## 启动容器
```shell
cd /_makeFile/nodered/
docker-compose up -d
```

### 低版本升级操作
:::info
💡 注意

在低版本升级过程中需要将nodered目录下载并解压放置在对应目录/_makeFile下。

:::

单机部署：

[nodered.zip](https://yunteng.yuque.com/attachments/yuque/0/2025/zip/36214471/1754892220986-8e5f220b-925c-4ad0-9d73-f046a446164a.zip)

微服务部署：

[nodered.zip](https://yunteng.yuque.com/attachments/yuque/0/2025/zip/36214471/1754892221072-95bd9aad-c2c3-4e4b-bee4-9c3619f447b5.zip)








