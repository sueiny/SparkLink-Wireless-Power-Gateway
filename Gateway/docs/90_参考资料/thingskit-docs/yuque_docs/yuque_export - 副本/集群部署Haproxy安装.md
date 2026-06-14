---
title: 集群部署Haproxy安装
---

# 安装Haproxy
## 二选一：docker镜像安装
:::info
💡 提示

haproxy镜像默认不支持UDP协议。

:::

### 启动
```shell
cd /_makeFile/loadbalance
docker-compose up -d --remove-orphans
```

### 查看启动日志
```shell
docker-compose logs  --tail=200 -f    #查看管理界面日志                                 
```

## 二选一：源码编译(不支持UDP协议)
:::info
💡 提示

安装脚本基于apt命令编写，推荐操作系统：Ubuntu。

:::

### 安装脚本赋予执行权限
```shell
cd /_makeFile/loadbalance
chmod +x build-haproxy-quic.sh
```

### 执行安装脚本
```shell
./build-haproxy-quic.sh
```

脚本成功执行结果如图

<img src="https://cdn.nlark.com/yuque/0/2025/png/13018922/1758694799460-3a1ac96b-6b13-49ed-acb2-ed4f8344927f.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_28%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="784" title="" crop="0,0,1,1" id="u9cd373a9" class="ne-image">



### 复制配置文件
```shell
mkdir /etc/haproxy/
mv ./conf/haproxy.cfg /etc/haproxy/
```

### 开机启动
```shell
sudo tee /etc/systemd/system/haproxy.service > /dev/null << 'EOF'
[Unit]
Description=HAProxy Load Balancer
Documentation=man:haproxy(1)
After=network.target

[Service]
Environment=CONFIG=/etc/haproxy/haproxy.cfg
EnvironmentFile=-/etc/default/haproxy
ExecStartPre=/usr/local/sbin/haproxy -f $CONFIG -c
ExecStart=/usr/local/sbin/haproxy -Ws -f $CONFIG
ExecReload=/usr/local/sbin/haproxy -f $CONFIG -c -q
ExecReload=/bin/kill -USR2 $MAINPID
KillMode=mixed
Restart=always
SuccessExitStatus=143
Type=notify

[Install]
WantedBy=multi-user.target
EOF
```

# 测试
:::warning
💡 提示

Haproxy访问地址：http:/访问服务的IP或域名/:9999/stats

账号/密码：账号和密码在配置文件中查看

:::

<img src="https://cdn.nlark.com/yuque/0/2023/png/13018922/1690957339285-aab28e8e-50f4-402e-8a3d-72cbefaee4ce.png?x-oss-process=image%2Fwatermark%2Ctype_d3F5LW1pY3JvaGVp%2Csize_87%2Ctext_VGhpbmdzS2l0%2Ccolor_FFFFFF%2Cshadow_50%2Ct_80%2Cg_se%2Cx_10%2Cy_10" width="2432.8" title="" crop="0,0,1,1" id="u08565936" class="ne-image">

```shell
#!/bin/bash
# test_udp_haproxy.sh

echo "=== HAProxy UDP 功能测试 ==="

# 测试 DNS 查询
echo "1. 测试 DNS UDP 负载均衡:"
dig @localhost example.com +short

# 测试 NTP 查询
echo -e "\n2. 测试 NTP UDP 负载均衡:"
ntpdate -q localhost

# 测试自定义 UDP 端口
echo -e "\n3. 测试自定义 UDP 服务:"
echo "test_message" | nc -u -w1 localhost 9999

# 检查统计信息
echo -e "\n4. 检查统计页面:"
curl -s http://localhost:8404/haproxy?stats | grep -i udp

echo -e "\n=== 测试完成 ==="
```


