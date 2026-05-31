# ThingsKit 凭证模式 Code Review

> 日期：2026-05-11  
> 关注点：access token 与 mqtt_basic 双模式保留

## 1. Review 结论

当前配置已按用户要求调整为：

```text
默认使用 access token
保留 mqtt_basic 旧凭证
通过 credential_mode 切换
```

当前默认：

```json
"credential_mode": "access_token"
```

这意味着 MQTT 登录时：

| MQTT 字段 | 值 |
|----------|----|
| client_id | 配置中的 `client_id` |
| username | 设备 access token |
| password | 空字符串 |

当切换为：

```json
"credential_mode": "mqtt_basic"
```

则使用：

```json
"mqtt_basic": {
  "client_id": "...",
  "username": "...",
  "password": "..."
}
```

## 2. 学习点：为什么要保留两种凭证

保留双模式的好处：

1. 当前平台已切换到 access token，可以按新方式测试。
2. 如果某些旧设备或旧产品仍用 MQTT basic，不需要回滚代码。
3. 脚本和板端读取同一个 `gateway_config.json`，减少“脚本能连、板端不能连”的问题。

## 3. 代码检查

### 3.1 `ConfigManager`

`ConfigManager` 的职责是把配置文件转换为运行时配置。

当前逻辑：

```text
credential_mode == mqtt_basic
    使用 mqtt_basic.client_id / username / password
否则
    username = access_token
    password = ""
```

这个边界正确。`MqttCloudClient` 不需要知道 access token 或 basic 的区别，它只拿最终的 `client_id/username/password` 去连接。

### 3.2 `thingskit_tree_test.py`

测试脚本也从 `gateway_config.json` 读取凭证模式。

这点很重要，因为脚本测试和板端测试必须使用同一套认证逻辑。否则可能出现：

```text
脚本上传成功，但板端失败
```

或：

```text
板端连接成功，但脚本误用旧凭证失败
```

## 4. 已验证内容

已用 access token 跑过：

```bash
python3 app/Gateway/gatewayd/things_model/model_scripts/thingskit_tree_test.py 1
```

结果：

```text
[OK] MQTT连接成功
[OK] 测试完成
```

## 5. 风险与建议

### 风险 1：access token 泄露

`gateway_config.json` 中包含真实 access token。对外打包或发文档时，应确认是否需要脱敏。

### 风险 2：平台侧凭证类型变化

如果平台设备凭证从 access token 改回 basic，必须同步修改：

```json
"credential_mode": "mqtt_basic"
```

### 风险 3：client_id 是否强绑定

当前使用配置中的 `client_id`。如果平台后续要求 access token 模式下 client_id 也必须为空或指定格式，需要再按平台要求调整。

## 6. Review 结论

当前实现满足“保留两种凭证，暂时选择 access token”的要求。

