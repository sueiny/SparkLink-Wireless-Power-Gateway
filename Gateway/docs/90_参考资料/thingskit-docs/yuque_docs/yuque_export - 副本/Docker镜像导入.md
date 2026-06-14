---
title: Docker镜像导入
---

# 第1/3步：查看服务器CPU信息
```shell
hostnamectl                                    
```

命令执行结果如下，红框内的是CPU信息：

<img src="https://cdn.nlark.com/yuque/0/2023/png/13018922/1689652281413-95dddbf8-56b8-4b2d-a07e-97212e06c796.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_15%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="431.2" title="" crop="0,0,1,1" id="u4a8d936a" class="ne-image">

# 第2/3步：导入镜像
将`_images`目录下对应CPU架构的离线镜像导入docker，<font style="color:rgb(38, 38, 38);">dokcer_imagesXXX.zip解压后可导入依赖软件镜像</font>。

## 二选一：单个导入
```shell
sudo docker load -i /_makeFile/_images/thingskit_microservice2.0.0_x86.tar  #服务器CPU架构多为x86-64
```

## 二选一：批量导入
:::info
💡 提示

批量导入脚本默认为X86CPU架构的部署包，ARM架构需要自己准备。

:::

```shell
find /_makeFile/_images -type f -name "*.tar"|xargs -r -I thingskit sudo docker load -i thingskit
```

# 第3/3步：导入结果
```shell
sudo docker images
```

<img src="https://cdn.nlark.com/yuque/0/2024/png/13018922/1731579425638-b2f0e323-a61e-4e44-a3a0-a455afbc3d49.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_28%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="777.6" title="" crop="0,0,1,1" id="u7e57d30e" class="ne-image">
