---
title: 设备MQTT接入
---

# 入门介绍
## MQTT基础知识
MQTT全称Message Queuing Telemetry Transport，它是一种基于消息队列的轻量级应用层通信协议，实现了消息发布和订阅。设备可以作为客户端的形式通过它来发布和接收消息，实现数据上报和实时控制。设计用于具有低带宽的受限设备。因此，它是物联网设备接入的完美解决方案。[您可以在此处](http://mqtt.org/)找到有关MQTT的更多信息。

ThingsKit平台提供了标准的MQTT接入协议，支持MQTTv3.1/v.5，任何支持MQTT协议的设备都可以通过相应的MQTT客户端代码接入云平台。

## MQTT身份认证
设备通过MQTT协议连接平台时，需要完成基于MQTT的身份认证，平台支持以下认证方式。

#### 普通认证方式
对于普通认证方式，在MQTT连接时，使用基于username/password的认证方式，需要用到设备的普通证书，如下：

| <font style="background-color:#F4F5F5;">MQTT连接参数</font> | <font style="background-color:#F4F5F5;">值</font> | <font style="background-color:#F4F5F5;">说明</font> |
| --- | --- | --- |
| username | `AccessToken` | 设备创建后自动生成，每个设备唯一，量产设备可通过API 自动获取`AccessToken`，实现一型一密。 |
| password | `ProjectKey` | 项目创建后自动生成，不支持修改。 |
| clientId | 空或任意 | 不对clientid做任何限制，可随意填写。 |


要注意的是，ThingsKit对同一个设备身份信息只支持一个MQTT连接，也就是说，如果在两个或多个物理设备中，使用同样的username/password身份信息连接平台，即便clientid使用不同的字符串，平台仍然将这些连接视为同一个设备，这会导致后一个设备连接成功后会顶掉之前的设备连接。

#### X.509TLS认证方式
在一些对通信安全要求严格的物联网领域，比如智能门锁、电表、水表、燃气表等，您可以使用基于X.509TLS的MQTT安全认证方式。

更进一步的物联网安全措施，可以在设备端集成SE安全芯片，或使用内置SE安全芯片的通信模组，实现设备和平台双向认证。

# 遥测上传主题
为了将遥测数据发布到ThingsKit服务器，请将PUBLISH消息发送到以下主题：

```plain
v1/devices/me/telemetry
```

数据格式：

```json
{"key1":"value1", "key2":"value2"}
```

或者

```json
[{"key1":"value1"}, {"key2":"value2"}]
```

:::color4
💡提示

在这种情况下，服务器端时间戳将自动分配给上传的数据！

:::

如果您的设备能够获取客户端时间戳，您可以使用以下格式： 

```json
{"ts":1451649600512, "values":{"key1":"value1", "key2":"value2"}}
```

# 控制接口RPC调用
## 服务器端调用RPC
为了从服务器订阅RPC命令，发送SUBSCRIBE消息到以下主题：

```plain
v1/devices/me/rpc/request/+
```

## 客户端调用RPC
为了向服务器发送RPC命令，向以下主题发送PUBLISH消息：

```plain
v1/devices/me/rpc/response/$request_id{request_id为订阅RPC的+}
```
