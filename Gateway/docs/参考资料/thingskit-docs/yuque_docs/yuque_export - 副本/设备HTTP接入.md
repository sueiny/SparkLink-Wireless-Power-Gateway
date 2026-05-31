---
title: 设备HTTP接入
---

## 入门介绍
### HTTP基础知识
HTTP是一种通用网络协议，可用于物联网应用程序。HTTP协议基于TCP，并使用请求-响应模型。

**ThingsKit**服务器节点充当支持HTTP和HTTPS协议的**HTTP服务器**。

对于一些非常单一的应用场景，比如只需要定期采集上报数据，不论是快速开发原型，还是小规模的应用，设备使用HTTP接入云平台也是不错的选择。

事实上，将HTTP协议的简单易用发挥到极致，便是CoAP协议，对于低功耗设备的单一数据上报，使用CoAP更加符合要求。

### HTTP身份验证和错误代码
我们将在本文中使用访问令牌设备凭证，稍后将它们称为$**ACCESS_TOKEN。**应用程序需要在每个HTTP请求中包含**$ACCESS_TOKEN**作为路径参数。可能的错误代码及其原因：

+  **400 无效请求** - 无效的URL、请求参数或正文。
+  **401 未经授权**- 无效的**$ACCESS_TOKEN**。
+  **404 未找到**- 未找到资源。

:::color4
💡 提示

设备使用HTTP接入ThingsKit平台，其产品协议使用**默认**即可。

:::

## 前提条件
设备已经在平台创建好了。如果设备比较多可以通过批量导入的方式添加设备，同时可以在批量导入时指定**设备凭证**即后续遥测要填写的**ACCESS_TOKEN。**

## 遥测数据上传接口
为了将遥测数据发布到ThingsKit服务器节点，请向以下URL发送POST请求：

```plain
http(s)://host:port/api/v1/$ACCESS_TOKEN/telemetry
```

最简单的支持数据格式是：

```json
{"key1":"value1", "key2":"value2"}
```

或者

```json
[{"key1":"value1"}, {"key2":"value2"}]
```

**请注意**，在这种情况下，服务器端时间戳将分配给上传的数据！

如果您的设备能够获取客户端时间戳，您可以使用以下格式：

```json
{"ts":1451649600512, "values":{"key1":"value1", "key2":"value2"}}
```

## 调用控制接口
### 服务器端下发RPC命令
通过浏览器进行的RPC命令订阅，订阅地址请参考**客户端订阅RPC命令**

### 客户端订阅RPC命令
用户可从服务器订阅RPC命令，发送带有可选“timeout”请求参数的GET请求到以下URL：

```plain
http(s)://host:port/api/v1/$ACCESS_TOKEN/rpc?timeout=2000
```

一旦订阅，如果没有对特定设备的请求，客户端可能会收到RPC请求或超时消息。RPC请求体示例如下所示：

```json
{
  "id": "1",
  "DO1":true
}
```

+ **id** - 请求id，整数请求标识符；可用于双向命令的响应。除开id以外的数据，均为用户输入的数据。

### 客户端RPC命令响应
<font style="color:rgb(33, 37, 41);">可以使用POST请求对以下URL进行回复：</font>

```plain
http(s)://host:port/api/v1/$ACCESS_TOKEN/rpc/{$id}
```

---
