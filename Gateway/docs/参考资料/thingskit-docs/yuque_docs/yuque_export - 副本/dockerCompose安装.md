---
title: dockerCompose安装
---

# 1/3:下载安装文件
## 确认CPU架构
```shell
hostnamectl
```

<img src="https://cdn.nlark.com/yuque/0/2024/png/13018922/1731481176441-3ee8db3a-0ba7-4d57-8ae6-cba4166ca8fc.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_14%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="484" title="" crop="0,0,1,1" id="u006e118c" class="ne-image">

根据操作系统和CPU架构下载对应系统的安装文件。

[Releases · docker/compose](https://github.com/docker/compose/releases)



docker-compose支持的系统和CPU结构清单如下图：

<img src="https://oss.yuntengcloud.com/iotdocs/img/image-20220726124929855.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_23%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="801" title="" crop="0,0,1,1" id="KF71X" class="ne-image">

## 在线
:::color3
⚠️ 警告

下载安装文件的命令，基于x86系统。arm系统需要调整下载链接。

:::

```shell
 curl -v https://github.com/docker/compose/releases/download/v2.7.0/docker-compose-linux-x86_64
```

```shell
ls         #查看安装包是否下载成功
```

## 离线(推荐)
:::info
💡 提示

如果使用命令直接下载比较慢或者无法下载，也可以使用我们提供的安装包，上传到服务器。

:::

:::color3
⚠️ 警告

需要登录后，语雀才允许下载相关资源。

:::

[docker-compose-linux-x86_64.rar](https://yunteng.yuque.com/attachments/yuque/0/2025/rar/36214471/1754892222301-1dc83bc7-a237-4d73-a102-790b0908f8e8.rar)

[docker-compose-linux-aarch64.rar](https://yunteng.yuque.com/attachments/yuque/0/2025/rar/36214471/1754892222434-d66b85dc-6683-4972-9aa5-753bc836006e.rar)

# 2/3:安装
:::color3
💡 提示

docker-compose版本不能低于19，低版本不能对环境变量中引用的环境变量进行应用。

:::

```shell
sudo mv docker-compose-linux-x86_64 /usr/bin/docker-compose     #重命名脚本文件
```

```shell
sudo chmod +x /usr/bin/docker-compose
```

# 3/3:验证docker-compose安装结果
```shell
sudo docker-compose version        #查看docker-compose是否安装成功
```

执行命令，可看到下图中的效果：

<img src="https://cdn.nlark.com/yuque/0/2024/png/13018922/1731481702056-d2222bca-8d07-4781-9f6a-197643b7de06.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_24%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="840" title="" crop="0,0,1,1" id="u79639aee" class="ne-image">
