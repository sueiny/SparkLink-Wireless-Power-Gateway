---
title: windows部署V2
---

:::color3
‼️注意

生成环境不推荐使用windows系统部署，接入数据量较少，推荐使用linux系统部署！

:::

[默认端口V2](https://yunteng.yuque.com/avshoi/v2xdocs/nr3n8nzk56sk4nmi)

更新指导：

[平台更新windows版本指导](https://yunteng.yuque.com/avshoi/v2xdocs/eam1fg0719uflzqm)

# 1、基础环境准备
## 1-1、安装java
:::warning
💡 提示

JDK版本不能低于<font style="color:rgb(38, 38, 38);">17.0.15</font>。安装文件请自行下载。

:::

参考安装jdk文档：

[JDK11安装指南](https://yunteng.yuque.com/avshoi/sqbbzn/fug1pf0d99yg3985)

## 1-2、安装Postgresql
:::warning
💡 提示

postgresql数据库版本建议16。部署包内默认postgres登录密码为thingskit。安装文件请自行下载。

:::

postgresql官网下载地址：

[EDB: Open-Source, Enterprise Postgres Database Management](https://www.enterprisedb.com/downloads/postgres-postgresql-downloads)

参考安装文档：

[Postgresql安装指南](https://yunteng.yuque.com/avshoi/sqbbzn/gyuuq9bhbfqv63x4)

### 补充：
:::warning
💡提示

pg数据库安装不会自带timescaleDB插件，这个需要手动安装。

:::

postgresql16数据库timescaleDB插件：

[timescaledb-postgresql-16-windows-amd64.zip](https://yunteng.yuque.com/attachments/yuque/0/2026/zip/36214471/1777448293235-42071de3-3e2a-4f47-a339-19f105aae20f.zip)

:::warning
💡提示

将数据库timescaleDB插件压缩包解压后。

将timescaleDB中sql和control文件放入数据库安装目录\share\extension。

将timescaleDB中dll文件放入数据库安装目录\lib。

:::

:::warning
💡提示

需要修改数据库配置项，将数据库配置文件postgresql.conf添加配置项shared_preload_libraries = 'timescaledb'。

postgresql.conf路径为数据库安装目录\data下。修改后重启数据库服务。

:::

:::warning
💡提示

重启数据库成功后，还需要使用navicat等工具主动创建需要使用的数据库timescaleDB模式。外部访问数据库还需要修改pg_hba.conf。

:::

```powershell
host    all				all				0.0.0.0/0				scram-sha-256			#允许外部通过密码访问数据库
```

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777454057119-5bad1b83-c12a-4c38-946a-54cf4fc60fad.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_62%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1233.7142857142858" title="" crop="0,0,1,1" id="ue3cddeb0" class="ne-image">

```powershell
CREATE EXTENSION IF NOT EXISTS timescaledb CASCADE;
```

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777449993634-535fa93b-be45-49a6-bcfd-5baeec190bf6.png?x-oss-process=image%2Fcrop%2Cx_0%2Cy_0%2Cw_1600%2Ch_529" width="1600" title="" crop="0,0,1,0.5882" id="u03ecd253" class="ne-image">

## 1-3、<font style="color:rgb(38, 38, 38);">安装golang</font>
:::warning
💡提示

安装文件请自行下载。源码部署可以不安装golang。推荐版本1.18.1。

:::

[Go语言依赖安装指南](https://yunteng.yuque.com/to2an3/sqbbzn/lypkxtb9zxch61z5)

## 1-4、安装Nginx
:::warning
💡 提示

推荐安装nginx版本为1.24。安装文件请自行下载。

:::

参考安装文档：

[Nginx安装指南](https://yunteng.yuque.com/avshoi/sqbbzn/kggg3p59uwd01pk0)

## 1-5、安装minio（可选）
:::warning
💡提示

使用minio作为文件管理服务需要修改window.bat对应配置才会被调用。

:::

```shell
FILE_STORAGE_TYPE					#修改为minio  默认local
```

参考安装文档：

[Minio安装指南](https://yunteng.yuque.com/to2an3/sqbbzn/yn8i7oz0od9p0tr0)

# 2、**<font style="color:rgb(38, 38, 38);">准备部署包</font>**
:::warning
<font style="color:rgb(38, 38, 38);">‼️</font> 特别注意

<font style="color:rgb(38, 38, 38);">将收到的部署包解压，其中zip文件后缀的压缩包都为前端文件，需要放置在nginx部署的目录下。并将web_server目录放在磁盘分区根目录。（默认为D盘）如果放在其他路径下，请修改window.bat脚本内对应配置。</font>

:::

:::warning
💡 注意

也可以将部署包内文件放在其他目录下，但需要根据放置路径修改window.bat以及nginx.conf的路径。

:::

# 3、配置脚本文件window.bat
## 3-1、数据库配置
:::warning
💡 注意

windows系统上部署，需要手动创建数据库，数据库默认名称为thingskit，可以设置为其他名称，但同时需要更改<font style="color:rgb(38, 38, 38);">window.bat内数据库名称。</font>

:::

:::warning
‼️ 注意

window.bat脚本内数据库的连接账号和密码都是默认的，其中账号为postgres，密码为thingskit，如果安装的数据库服务不一致，则需要修改对应信息。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777357810622-64a113f5-d54d-4c1c-9c8e-742fbd8a0b03.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_68%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1362.2857142857142" title="" crop="0,0,1,1" id="u315b5102" class="ne-image">

:::warning
💡 提示

数据库创建好之后，双击脚本文件window.bat运行，即可初始化数据库。

:::

## 3-2、证书配置
:::warning
💡 提示

需要<font style="color:rgb(38, 38, 38);">向平台相关人员提供部署服务器的MAC地址，以生成对应的license证书。可以按照以下步骤确定使用的网卡以及MAC地址。</font>

:::

```powershell
ipconfig				    #在cmd中输入打印网卡信息
getmac /v /fo list	#在cmd中输入打印mac地址
```

:::warning
💡 提示

由平台发放证书后，将lic格式文件存放至_cert目录下。

:::

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1724643175676-2b4e5dfe-515f-47c0-bd3f-a09f5224af5e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_38%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="716.2666666666667" title="" crop="0,0,1,1" id="u5b395bb6" class="ne-image">

# 4、启动物联网平台服务
:::warning
💡 注意

启动服务时会首先执行初始化数据库的动作，然后再运行平台服务。

:::

:::warning
💡 提示

首次部署或升级部署时，需要先删除可执行文件xjar，对应的部署包thingsKit.xjar和xjar.go必须一对一匹配。

:::

:::warning
💡 提示

双击window.bat启动服务。双击后会弹出一个cmd窗口，请在平台使用过程中保持窗口运行，需要关闭平台时可以关闭这个cmd窗口。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777358128945-164025d0-f403-47e4-9141-d0128efb20c4.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_38%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="766.8571428571429" title="" crop="0,0,1,1" id="u25b8379f" class="ne-image">

# 5、启动nginx服务
## 5-1、更新Nginx配置文件
:::warning
💡 提示

将前端文件data_view.zip、scada.zip、web_ui.zip放在nginx部署目录下，然后将nginx.conf修改为以下内容。data_view.zip，scada.zip需要购买额外扩展模块才会提供。

:::

部署路径为此方式即可：

<img src="https://cdn.nlark.com/yuque/0/2024/png/36214471/1724644275245-476aaf43-627a-4882-a694-b6416a4e018e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_39%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="728" title="" crop="0,0,1,1" id="u6b18b6d5" class="ne-image">

```powershell

#user  nobody;
worker_processes  1;

#error_log  logs/error.log;
#error_log  logs/error.log  notice;
#error_log  logs/error.log  info;

#pid        logs/nginx.pid;


events {
    worker_connections  1024;
}
#http{


http {
    include       mime.types;
    default_type  application/octet-stream;

    #log_format  main  '$remote_addr - $remote_user [$time_local] "$request" '
    #                  '$status $body_bytes_sent "$http_referer" '
    #                  '"$http_user_agent" "$http_x_forwarded_for"';

    #access_log  logs/access.log  main;

    sendfile        on;
    #tcp_nopush     on;

    #keepalive_timeout  0;
    keepalive_timeout  65;

    #gzip  on;

    server {
        listen       9527;
        server_name  localhost;
        charset utf-8;
        #access_log  logs/host.access.log  main;

        location / {
            root   web_ui;
            index  index.html index.htm;
            try_files $uri $uri/ /index.html;
        }
		location /thingskit-scada {
            alias   scada;
            index  index.html index.htm;
            try_files $uri $uri/ /index.html;                 
        }
        location /large-designer {
            alias   data_view;
            index  index.html index.htm;
            try_files $uri $uri/ /index.html;
           # add_header Cache-Control "no cache,no store";
        }
        location /red/ {
                        proxy_pass  http://localhost:1880/;
                        proxy_set_header Host $host;
                        proxy_set_header X-Real-IP $remote_addr;
                        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
                        proxy_set_header X-Forwarded-Proto $scheme;
        }		
		
	    location /api/ {
	        proxy_set_header Host $http_host;
            proxy_set_header X-Forward-For $remote_addr;
            proxy_pass  http://localhost:8080/api/;
            proxy_ssl_server_name on;
        
	    }
        location /api/ws {
           proxy_pass  http://localhost:8080;
           proxy_connect_timeout  5s;
           proxy_read_timeout  300s;
           proxy_send_timeout  300s;
           proxy_redirect off;
           #下面三行是重点
           proxy_http_version      1.1;
           proxy_set_header Upgrade $http_upgrade;
           proxy_set_header Connection "upgrade";
	       #编辑传递给代理服务器的请求头  
           proxy_set_header Host $http_host;                            #原始请求的主机名
           proxy_set_header X-Real-IP        $remote_addr;              #调用当前代理的上级客户端的IP地址
           proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for; #显示代理树的全部客户端IP地址
	       proxy_set_header X-Forwarded-Proto $scheme;                  #原始请求的协议
	       proxy_set_header X-Forwarded-host $server_name;              #原始请求的Host值
	       proxy_set_header X-Forwarded-Port $server_port;              #原始请求的端口
        }
  }
}
```

## 5-2、启动服务
:::warning
💡 提示

<font style="color:rgb(38, 38, 38);">首先进入cmd，切换到nginx安装的路径下，执行以下命令。也可以直接双击nginx.exe。</font>

:::

```powershell
start nginx
```

# 6、测试ThingsKit物联网平台是否安装成功
:::color3
💡注意

设备分布等功能调用地区需要获取高德地图api接口配置。windows部署与linux部署前端文件的目录会有些不一样，需要重新配置的目录为nginx目录下的\web_ui\_app.config.js。且需要手动重启nginx服务。

:::

操作流程：

[安装部署问题](https://yunteng.yuque.com/avshoi/v2xdocs/gho03tgxganir968#QDsmZ)

物联网平台、管理页面都成功部署后。我们就可以开始使用系统了。

:::info
💡 提示

访问地址：http://平台部署服务器IP或域名:9527

超级管理员账号:sysadmin

超级管理员密码:Sysadmin@123

租户管理员/客户默认密码：123456

:::

:::info
‼️ 注意

<font style="color:rgb(38, 38, 38);">设备的接入需要再超级管理员的账号登录后，创建租户-租户管理员并访问租户账号才能使用。</font>

:::

## 平台是否安装成功验收清单
1. 默认账号是否能成功登录。
2. 默认账号是否可以在菜单**租户角色**中新建租户角色。
3. 默认账号是否可以在菜单**租户列表**中新建租户。
4. 默认账号是否可以为租户**租户管理员。**
5. 租户管理员是否可以在菜单**平台定制**中上传LOGO图片。
6. 租户管理员是否可以在菜单**账号管理**中新建客户。
7. 租户管理员是否可以在菜单**组织管理**中新建组织。
8. 租户管理员是否可以在菜单**设备管理**>**产品**中创建产品。
9. 租户管理员是否可以在菜单**设备管理**>**产品**>**详情**>**物模型管理**中编辑和发布物模型。
10. 租户管理员是否可以在菜单**设备管理**>**设备列表**中新建设备。
11. 设备连接平台后**设备管理**>**设备列表**中对应设备的**状态**是否为在线。
12. 设备推送遥测数据后**设备管理**>**设备列表**>**详情**中的**物模型数据**是否可以看到最新的遥测数据。

# 扩展：<font style="color:rgb(38, 38, 38);">启动nodered支持平台嵌入使用</font>
:::warning
‼️ 特别注意

该模块属于增值功能，不包含在基础功能内，部署文件需要额外获取。

:::

## 安装环境
:::info
💡 提示

<font style="color:rgb(38, 38, 38);">首先需要下载并解压Nodejs安装文件，双击msi文件进行安装。</font>

:::

<font style="color:rgb(38, 38, 38);">可使用本文档提供的安装文件以及对应版本：</font>

[node-v22.14.0-x64.zip](https://yunteng.yuque.com/attachments/yuque/0/2025/zip/36214471/1754892220272-9bdf1276-e16e-4c7d-8f63-b5937b6975c6.zip)

:::info
💡 提示

<font style="color:rgb(38, 38, 38);">如何验证是否安装Nodejs成功？</font>

:::

<font style="color:rgb(38, 38, 38);">能够正常在cmd中打印版本即可：</font>

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1743405350637-3c56dbe2-fc85-455e-988c-5e84fb9c1d1a.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_11%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="245.71429687292448" title="" crop="0,0,1,1" id="u7beccbbd" class="ne-image">

## 获取部署脚本
:::info
💡 提示

nodered的部署脚本请咨询相关人员获取。1.5.2版本以及后续版本自带无需重复获取。

:::

## 部署方式
:::info
‼️ 注意

本文档部署方式默认文件存放目录为D:\web_server\nr，如果修改文件或调整了端口、数据库名称登录方式等请咨询相关人员修改配置。

:::

:::info
💡 提示

启动前需要修改配置文件。

:::

修改文件D:\web_server\nr\.env：

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777356886283-f5d14559-5d9b-40fa-bf8c-d06d2714f0e7.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_49%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="983.4285714285714" title="" crop="0,0,1,1" id="ubb8d5e3f" class="ne-image">

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777363314893-5764e966-ed92-42c7-9995-5414d25c294e.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_70%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1408.5714285714287" title="" crop="0,0,1,1" id="uec3a2d8e" class="ne-image">

## 启动服务
:::info
💡 提示

<font style="color:rgb(38, 38, 38);">双击nodered.bat启动服务。</font>

:::

<img src="https://cdn.nlark.com/yuque/0/2025/png/36214471/1743580063782-a4c564e8-4dc8-4f81-9296-95793ad14685.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_40%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="883.809563946023" title="" crop="0,0,1,1" id="u16e22e58" class="ne-image">

# 扩展：**<font style="color:rgb(38, 38, 38);">启动GBT28181协议的支撑软件ZLMediaKit</font>**
:::color3
‼️特别注意

该模块属于增值功能，不包含在基础功能内，部署文件需要额外获取。

:::

:::color3
‼️特别注意

视频接入功能GBT28181默认是关闭的。启用该功能需要将启动脚本window.bat中【GBT28181_ENABLED】的值改为【true】。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777358904760-84d1a1a0-42dc-454c-83ea-33a4f130d019.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_65%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1309.142857142857" title="" crop="0,0,1,1" id="ua0f249b3" class="ne-image">

## 获取流媒体服务文件
:::warning
💡提示

需要从相关人员处获取流媒体部署文件，并解压到web_server目录。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777359515759-d6e958a2-574e-41f3-a394-d3eedc893b36.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_38%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="764" title="" crop="0,0,1,1" id="uea63807e" class="ne-image">

## 启动流媒体服务
:::warning
💡提示

首先执行VC_redist.x64_3.exe按照刘流媒体依赖，然后进入zlm目录后，双击MediaServer.exe启用流媒体服务，会打开一个cmd窗口，流媒体运行中请勿关闭此cmd窗口。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777359678999-ec75b81b-88e6-46db-a6cc-bfb301da4c34.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_62%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1244.5714285714287" title="" crop="0,0,1,1" id="u5a557b8c" class="ne-image">

## 修改配置
:::warning
💡提示

需要按照当前部署环境自行修改配置项。

:::

<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777359198533-c780cb66-b14b-4400-850f-b16c0ae8af19.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_55%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1106.2857142857142" title="" crop="0,0,1,1" id="u2025df71" class="ne-image">

## 重启平台后台
:::warning
💡提示

前置步骤完成后，需要关闭平台启动的cmd窗口后，双击window.bat重新启动平台。

:::

平台cmd窗口：<img src="https://cdn.nlark.com/yuque/0/2026/png/36214471/1777360087581-8d645c52-a9a4-428b-aac4-2ea9bd1f3237.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_63%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="1264" title="" crop="0,0,1,1" id="u78e9a29c" class="ne-image">

# 扩展：源码打包windows部署调整
:::color3
💡提示：

需要修改window.bat启动脚本的内容。

:::

```shell
@ECHO OFF
chcp 65001
setlocal ENABLEEXTENSIONS



rem 配置环境变量
set EDGES_ENABLED=true
set TB_SERVICE_TYPE=monolith
set SPRING_DATASOURCE_URL=jdbc:postgresql://localhost:5432/thingskit
set SPRING_DATASOURCE_USERNAME=postgres
set SPRING_DATASOURCE_PASSWORD=thingskit

set ZOOKEEPER_ENABLED=false
set CACHE_TYPE=caffeine
set TB_QUEUE_TYPE=in-memory
rem cassandra, sql, or timescale (for hybrid mode, DATABASE_TS_TYPE value should be cassandra, or timescale)
set DATABASE_TS_TYPE=sql
set DATABASE_TS_LATEST_TYPE=sql

set MINIO_URL=http://localhost:9000
set MINIO_NAME=minioadmin
set MINIO_PWD=minioadmin
set FILE_STORAGE_TYPE=local

set LICENSE_SUBJECT=yunteng
set LICENSE_PATH=D:\web_server\_cert\
set LICENSE_STORE_PASS=Sys@admin1234

set GBT28181_ENABLED=false


rem 初始化数据库：开始
cd data/sql
go build xjar.go
rem xjar java  -Dinstall.data_dir=D:\web_server\data -Dlogging.config=D:\web_server\_logback.xml --add-opens java.base/jdk.internal.loader=ALL-UNNAMED -jar initDb.xjar
java -Dfile.encoding=UTF-8 --add-opens java.base/jdk.internal.loader=ALL-UNNAMED  --add-opens java.base/java.lang=ALL-UNNAMED --add-opens java.base/java.net=ALL-UNNAMED  -Dinstall.data_dir=D:\web_server\data -jar ./initDb.jar
rem 初始化数据库：结束

rem 启动物联网平台服务端：开始
cd ../..
go build xjar.go
rem xjar java -Dinstall.data_dir=D:\web_server\data  -Dlogging.config=D:\web_server\_logback.xml --add-opens java.base/jdk.internal.loader=ALL-UNNAMED -jar thingsKit.xjar
java -Dapp.name=thingskit --add-opens java.base/jdk.internal.loader=ALL-UNNAMED  --add-opens java.base/java.lang=ALL-UNNAMED --add-opens java.base/java.net=ALL-UNNAMED -Dinstall.data_dir=D:\web_server\data   -Dlogging.config=D:\web_server\_logback.xml -jar ./thingsKit.jar
rem 启动物联网平台服务端：结束

pause
```
