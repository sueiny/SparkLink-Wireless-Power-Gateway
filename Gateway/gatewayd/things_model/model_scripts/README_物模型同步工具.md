# ThingsKit 物模型辅助工具

## 功能

辅助管理 ThingsKit 产品和设备配置，支持：
- 自动创建产品
- 自动创建设备
- 刷新/重建设备配置文件(Device Profile)
- 删除并重建目标设备
- 修复设备类型、产品绑定和网关关系

## 产品与物模型边界

脚本可以通过 API 自动创建产品和设备。

但如果产品是由脚本首次创建的，该产品的物模型不能直接视为已经在 ThingsKit 页面完成正常导入和发布，页面显示可能不完整或不符合预期。需要先在 ThingsKit 页面手动导入一次该产品的物模型。

完成首次手动导入后，脚本再对该产品执行刷新、修改或重建，物模型内容才能在平台页面正常显示和使用。

推荐流程：

1. 修改 `gatewayd/things_model/*.json`。
2. 如果产品不存在，可以用脚本先创建产品和设备。
3. 对脚本新建的产品，在 ThingsKit 页面手动导入一次物模型。
4. 手动导入完成后，再运行脚本刷新/重建产品配置、设备配置和设备关系。
5. 后续已手动导入过的产品，可以继续用脚本做修改、刷新和重建。
6. 运行模拟数据脚本验证上报。

## 使用方法

```bash
cd /home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/things_model/model_scripts

# 预览模式（不实际执行）
python3 thingskit_model_sync.py --dry-run --user 1 --password 'Sztu@123456'

# 刷新/重建；脚本新建产品需先在平台手动导入一次物模型
python3 thingskit_model_sync.py --user 1 --password 'Sztu@123456'
```

## 命令行参数

| 参数 | 说明 | 示例 |
|------|------|------|
| --user | 用户名 | --user 1 |
| --password | 密码 | --password 'Sztu@123456' |
| --url | ThingsKit地址 | --url https://thingskit.example.com |
| --gateway-config | gateway配置文件路径 | --gateway-config path/to/config.json |
| --model-dir | 物模型目录 | --model-dir path/to/things_model |
| --dry-run | 预览模式 | --dry-run |

## 自动读取配置

脚本会自动从 `gateway_config.json` 读取：
- ThingsKit地址（从 `thingskit.host`）
- 用户名（从 `thingskit.username`）

密码需要通过命令行参数指定。

## 执行结果示例

```
[OK] 登录成功
[OK] 加载产品配置成功: 5 个产品
[OK] 加载已有配置文件: 25 个

==================================================
同步产品: 单相电表 (single_phase_meter)
==================================================
  属性: 14 个
  事件: 3 个
  服务: 2 个
  [UPDATE] 刷新配置...
  [OK] 执行成功

==================================================
执行完成: 5/5 个产品处理成功
==================================================
```

## 物模型文件

物模型文件位于 `gatewayd/things_model/` 目录：

| 文件 | 产品 |
|------|------|
| all_product_models.json | 产品配置总表 |
| gateway_model.json | 网关 |
| single_phase_meter_model.json | 单相电表 |
| env_sensor_model.json | 温湿度传感器 |
| relay_device_model.json | 继电器 |
| dtu_node_model.json | DTU节点 |

## 注意事项

1. 首次运行建议用 `--dry-run` 预览
2. 脚本可以创建产品和设备
3. 脚本新建产品需要先在 ThingsKit 页面手动导入一次物模型
4. 已手动导入过物模型的产品，后续可以用脚本修改、刷新和重建
5. 脚本主要用于刷新重建、创建/修改设备和修复关系
6. 密码不要写在脚本中，通过命令行传入
