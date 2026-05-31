---
title: 创建数据库ThingsKit
---

# 二选一：命令行(PGSQL)
```plsql
sudo docker exec -it pgsql bash           #进入docker容器

```

```plsql
psql -U postgres -d postgres -c "CREATE DATABASE thingskit";

```

# <font style="color:rgb(31, 9, 9);">二选一：数据库管理工具(例如：navicat)</font>
:::warning
💡<font style="color:rgb(31, 9, 9);"> 提示</font>

<font style="color:rgb(31, 9, 9);">低版本的数据库管理工具与数据库存在兼容性问题，</font>

<font style="color:rgb(31, 9, 9);">例如：Navicat版本不能低于17，低版本不兼容pgsql15。</font>

:::

<font style="color:rgb(31, 9, 9);">远程连接数据库。查询窗口执行SQL语句创建数据库实例</font><font style="color:rgb(31, 9, 9);background-color:rgb(218, 218, 218);">thingsKit</font><font style="color:rgb(31, 9, 9);">。</font>

```plsql
CREATE DATABASE "thingskit" ;
-- WITH OWNER "postgres" ENCODING 'UTF8' LC_COLLATE = 'en_US.UTF-8' LC_CTYPE = 'en_US.UTF-8' TEMPLATE template0;
```

<font style="color:rgb(31, 9, 9);">执行命令后生成数据库信息如下图：</font>

<img src="https://cdn.nlark.com/yuque/0/2023/png/13018922/1690709301892-970da2ae-113a-4c79-a4dd-c2ada2eb05c5.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_16%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="549" title="" crop="0,0,1,1" id="i1gMH" class="ne-image">


