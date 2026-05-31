# sle_data_app

`sle_data_app` 是 Gateway 第一阶段接入真实 SLE 数据源的独立测试 APP。它不接入 `gatewayd`，不做 Mesh/Modbus 解析，也不做 ThingsKit 上云；当前目标只做 WS73 Linux 用户态 SLE client 的一对多连接、服务发现和 notify/indication 数据打印。

本 APP 以已验证可连接 WS73 板端环境的 `app_sample/sle/sle_uuid_client` 为基础；`dtu_smaple/sle_one_to_many` 只作为 WS63/LiteOS 的 API 流程参考，不照搬它的连接表实现。

## 参数在哪里改

优先改板端运行配置：

```text
/userdata/gateway/config/sle_data_app.json
```

仓库里的默认配置文件是：

```text
app/Gateway/sle_data_app/sle_data_app.json
```

编译进程序的默认值集中在：

```text
app/Gateway/sle_data_app/inc/sle_app_config.h
```

建议调试时先改 JSON；确认稳定后，再把默认值同步到 `sle_app_config.h` 顶部的 `SLE_APP_DEFAULT_*` 常量区。

## 参考来源

- Silicon Labs 多连接文档强调：连接打开后必须保存 connection handle，达到最大连接数后不要继续打开新连接：https://docs.silabs.com/bluetooth/6.0.0/bluetooth-fundamentals-connections/multi-central-topology
- ESP-IDF GATT client 文档说明多连接 demo 可连接多个 GATT server，并且 MTU、服务发现、读写都以 `conn_id` 为参数：https://docs.espressif.com/projects/esp-idf/en/v3.3.1/api-reference/bluetooth/esp_gattc.html
- Nordic multilink NUS central 使用 connection index 管理多个 peripheral：https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/ble-nrf51-multilink-nus-central-connect-to-many-pe

这些资料只提炼“一对多连接管理”的架构思想，不复制第三方代码。

## 目录

```text
sle_data_app/
  Makefile
  sle_data_app.json
  inc/
  src/
  docs/
    architecture.md
    connection_flow.md
    server_connections.md
    sle_params.md
    test_plan.md
```

## 编译

```bash
make -C app/Gateway/sle_data_app \
  CROSS=/home/sueiny/rk3506_linux6.1_v1.2.0/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
```

## 推送与运行

```bash
adb push app/Gateway/sle_data_app/sle_data_app /userdata/gateway/bin/
adb push app/Gateway/sle_data_app/sle_data_app.json /userdata/gateway/config/
adb shell 'chmod +x /userdata/gateway/bin/sle_data_app'
adb shell 'timeout 120 /userdata/gateway/bin/sle_data_app --config /userdata/gateway/config/sle_data_app.json'
```

## 当前阶段边界

- 做：最多 8 个 SLE server 连接、按 MAC 前缀过滤 DTU、独立 `server_index`、MTU、服务发现、notify/indication 打印。
- 不做：大流量压测、`sle_set_mcs`、`sle_set_phy_param`、主动 `sle_set_data_len`、真实网关 IPC、Mesh/Modbus 解析、设备状态入库。
