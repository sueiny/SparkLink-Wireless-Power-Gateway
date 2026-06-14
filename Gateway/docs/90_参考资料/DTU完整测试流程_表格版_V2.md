用途：把 UART / BLE / 配置模式 / 运行模式 /
持久化验证整理成可直接填写的测试表

# 1. 测试前提表

| **项目**           | **要求/当前实现**                               | **检查方式/备注**                                  |
|--------------------|-------------------------------------------------|----------------------------------------------------|
| 模式来源           | GPIO13 拨码                                     | 高电平=CONFIG；低电平=RUN。                        |
| UART 配置口        | UART_BUS_0                                      | 当前代码实际配置口。                               |
| UART 引脚          | TX=GPIO17；RX=GPIO18                            | 接线时交叉连接：板 TX 接工具 RX，板 RX 接工具 TX。 |
| 默认串口参数       | 115200 8N1                                      | 串口工具打开十六进制发送/接收。                    |
| 协议帧格式         | AA 55 CMD SEQ LEN_L LEN_H BODY CRC_L CRC_H      | CRC 为 Modbus CRC16。                              |
| 响应格式           | AA 55 RESP_CMD SEQ LEN_L LEN_H BODY CRC_L CRC_H | RESP_CMD=CMD\|0x80；BODY\[0\]=status。             |
| BLE Service        | 0xFDF0                                          | BLE 配置通道使用。                                 |
| BLE Characteristic | 0xFDF1                                          | 属性：Write Without Response + Notify。            |
| BLE Notify         | 先写 CCCD(0x2902)=0x0001                        | 不打开 Notify 时可能写入成功但收不到响应。         |
| SEQ/CRC 说明       | 本表固定使用一套 SEQ 与 CRC                     | 如果自己改 SEQ，CRC 必须重新计算。                 |

# 2. 指令总表（固定报文版）

| **分类** | **指令**                      | **CMD** | **发送 Hex**                                                      | **基本预期**                                   |
|----------|-------------------------------|---------|-------------------------------------------------------------------|------------------------------------------------|
| 读取     | READ_DEV_INFO                 | 0x01    | AA 55 01 01 00 00 50 18                                           | 响应 0x81；status=00；返回 role/mac/name（设备自身 name，非白名单 name）。     |
| 读取     | READ_UART_CFG                 | 0x02    | AA 55 02 02 00 00 A0 5C                                           | 响应 0x82；status=00；默认 07 00 01 08。       |
| 读取     | READ_MODBUS_CFG               | 0x03    | AA 55 03 03 00 00 F0 60                                           | 响应 0x83；status=00；默认 item_count=0。      |
| 读取     | READ_ROOT_WL_ALL              | 0x04    | AA 55 04 04 00 00 40 D5                                           | ROOT 返回白名单；NODE 返回 status=06。         |
| 读取     | READ_ROOT_POWER               | 0x05    | AA 55 05 05 00 00 10 E9                                           | ROOT 返回 power；NODE 返回 status=06。         |
| 读取     | GET_MODE_STATUS               | 0x06    | AA 55 06 06 00 00 E0 AD                                           | 响应 0x86；status=00；返回当前模式与配置状态。 |
| 写入     | SET_ROLE(ROOT)                | 0x10    | AA 55 10 06 01 00 01 75 4B                                        | 响应 0x90；status=00。                         |
| 写入     | SET_ROLE(NODE)                | 0x10    | AA 55 10 06 01 00 00 B4 8B                                        | 响应 0x90；status=00。                         |
| 写入     | SET_UART_CFG(115200,None,1,8) | 0x11    | AA 55 11 07 04 00 07 00 01 08 F6 AD                               | 响应 0x91；status=00；重启后串口配置生效。     |
| 写入     | SET_MODBUS_CFG(2项)           | 0x12    | AA 55 12 08 05 00 02 01 02 05 03 10 5B                            | 响应 0x92；status=00；再读应返回 2 项。        |
| ROOT     | SET_ROOT_POWER(5)             | 0x13    | AA 55 13 09 01 00 05 33 9C                                        | ROOT 下 status=00；NODE 下 status=06。         |
| ROOT     | ADD_WL_ITEM                   | 0x14    | AA 55 14 0A 06 00 A1 A2 A3 A4 A5 A6 B6 E8 | ROOT 下添加 MAC=A1:A2:A3:A4:A5:A6。                          |
| ROOT     | DEL_WL_ITEM                   | 0x15    | AA 55 15 0B 06 00 A1 A2 A3 A4 A5 A6 EA BD                         | 删除指定 MAC；找不到返回 08。                  |
| ROOT     | CLEAR_WL                      | 0x16    | AA 55 16 0C 00 00 C4 6F                                           | ROOT 下清空白名单。                            |
| 控制     | COMMIT                        | 0x20    | AA 55 20 0D 00 00 9A 27                                           | 响应 0xA0；status=00；写入 NV。                |
| 控制     | REBOOT                        | 0x21    | AA 55 21 0E 00 00 6B DB                                           | 响应 0xA1；status=00；随后重启。               |
| 控制     | FACTORY_RESET                 | 0x22    | AA 55 22 0F 00 00 3A 5F                                           | 响应 0xA2；status=00；恢复默认。               |

# 3. 配置模式基础读取测试表

| **步骤** | **测试项**        | **发送 Hex**            | **期望响应/结果**                    | **实际响应 Hex** | **是否通过** | **备注**                                   |
|----------|-------------------|-------------------------|--------------------------------------|------------------|--------------|--------------------------------------------|
| 1        | GPIO13 置高并上电 | \-                      | 进入 CONFIG 模式                     |                  |              | 先确认拨码状态。                           |
| 2        | READ_DEV_INFO     | AA 55 01 01 00 00 50 18 | 0x81；status=00；返回 role/mac/name（设备自身 name，非白名单 name）  |                  |              | 设备名可能是 Kconfig 配置名或 DTU_xxxxxx。 |
| 3        | READ_UART_CFG     | AA 55 02 02 00 00 A0 5C | 0x82；status=00；默认 07 00 01 08    |                  |              | 即 115200,None,1,8。                       |
| 4        | READ_MODBUS_CFG   | AA 55 03 03 00 00 F0 60 | 0x83；status=00；默认 item_count=0   |                  |              | 如已配置则返回配置项。                     |
| 5        | GET_MODE_STATUS   | AA 55 06 06 00 00 E0 AD | 0x86；status=00；current_mode=CONFIG |                  |              | 确认模式来源为拨码。                       |

# 4. 基础写入与读回测试表

| **步骤** | **测试项**               | **发送 Hex**                           | **期望响应**    | **读回验证**                            | **实际响应 Hex** | **是否通过** |
|----------|--------------------------|----------------------------------------|-----------------|-----------------------------------------|------------------|--------------|
| 1        | SET_ROLE(ROOT)           | AA 55 10 06 01 00 01 75 4B             | 0x90；status=00 | 再发 READ_DEV_INFO，role 应为 ROOT      |                  |              |
| 2        | SET_UART_CFG(115200 8N1) | AA 55 11 07 04 00 07 00 01 08 F6 AD    | 0x91；status=00 | 再发 READ_UART_CFG，返回 07 00 01 08    |                  |              |
| 3        | SET_MODBUS_CFG(2项)      | AA 55 12 08 05 00 02 01 02 05 03 10 5B | 0x92；status=00 | 再发 READ_MODBUS_CFG，返回 item_count=2 |                  |              |
| 4        | SET_ROLE(NODE)           | AA 55 10 06 01 00 00 B4 8B             | 0x90；status=00 | 再发 READ_DEV_INFO，role 应为 NODE      |                  |              |

# 5. ROOT 专属功能测试表

| **步骤** | **测试项**        | **前置条件**         | **发送 Hex**                                                      | **期望响应/结果**                             | **实际响应 Hex** | **是否通过** |
|----------|-------------------|----------------------|-------------------------------------------------------------------|-----------------------------------------------|------------------|--------------|
| 1        | 切换 ROOT         | CONFIG 模式          | AA 55 10 06 01 00 01 75 4B                                        | status=00                                     |                  |              |
| 2        | SET_ROOT_POWER(5) | 当前为 ROOT          | AA 55 13 09 01 00 05 33 9C                                        | status=00                                     |                  |              |
| 3        | READ_ROOT_POWER   | 当前为 ROOT          | AA 55 05 05 00 00 10 E9                                           | status=00；power=05                           |                  |              |
| 4        | ADD_WL_ITEM       | 当前为 ROOT          | AA 55 14 0A 06 00 A1 A2 A3 A4 A5 A6 B6 E8 | status=00；添加 MAC=A1:A2:A3:A4:A5:A6                       |                  |              |
| 5        | READ_ROOT_WL_ALL  | 当前为 ROOT          | AA 55 04 04 00 00 40 D5                                           | status=00；能读到 A1:A2:A3:A4:A5:A6 |                  |              |
| 6        | DEL_WL_ITEM       | 当前为 ROOT 且已添加 | AA 55 15 0B 06 00 A1 A2 A3 A4 A5 A6 EA BD                         | status=00；再次读取应不存在                   |                  |              |
| 7        | ADD_WL_ITEM       | 当前为 ROOT          | AA 55 14 0A 06 00 A1 A2 A3 A4 A5 A6 B6 E8 | status=00；重新添加                           |                  |              |
| 8        | CLEAR_WL          | 当前为 ROOT          | AA 55 16 0C 00 00 C4 6F                                           | status=00；白名单清空                         |                  |              |
| 9        | READ_ROOT_WL_ALL  | 当前为 ROOT          | AA 55 04 04 00 00 40 D5                                           | status=00；wl_total=0 或 item_count=0         |                  |              |

# 6. 角色限制测试表

| **步骤** | **测试项**          | **前置条件** | **发送 Hex**                                                      | **期望结果**            | **实际响应 Hex** | **是否通过** |
|----------|---------------------|--------------|-------------------------------------------------------------------|-------------------------|------------------|--------------|
| 1        | 切换 NODE           | CONFIG 模式  | AA 55 10 06 01 00 00 B4 8B                                        | status=00               |                  |              |
| 2        | NODE 读 ROOT 白名单 | 当前为 NODE  | AA 55 04 04 00 00 40 D5                                           | status=06 ROLE_MISMATCH |                  |              |
| 3        | NODE 读 ROOT 功率   | 当前为 NODE  | AA 55 05 05 00 00 10 E9                                           | status=06 ROLE_MISMATCH |                  |              |
| 4        | NODE 设置 ROOT 功率 | 当前为 NODE  | AA 55 13 09 01 00 05 33 9C                                        | status=06 ROLE_MISMATCH |                  |              |
| 5        | NODE 新增白名单     | 当前为 NODE  | AA 55 14 0A 06 00 A1 A2 A3 A4 A5 A6 B6 E8 | status=06 ROLE_MISMATCH |                  |              |

# 7. COMMIT + REBOOT 持久化测试表

| **步骤** | **操作**             | **发送 Hex**                                                      | **期望结果**                     | **实际结果** | **是否通过** | **备注**               |
|----------|----------------------|-------------------------------------------------------------------|----------------------------------|--------------|--------------|------------------------|
| 1        | 配置为 ROOT          | AA 55 10 06 01 00 01 75 4B                                        | status=00                        |              |              | 准备持久化 ROOT 配置。 |
| 2        | 设置串口参数         | AA 55 11 07 04 00 07 00 01 08 F6 AD                               | status=00                        |              |              | 当前串口不立即重配。   |
| 3        | 设置 Modbus 预设     | AA 55 12 08 05 00 02 01 02 05 03 10 5B                            | status=00                        |              |              | 2 个设备项。           |
| 4        | 设置 ROOT 功率       | AA 55 13 09 01 00 05 33 9C                                        | status=00                        |              |              | power=5。              |
| 5        | 新增白名单           | AA 55 14 0A 06 00 A1 A2 A3 A4 A5 A6 B6 E8 | status=00                        |              |              | 添加 MAC=A1:A2:A3:A4:A5:A6。         |
| 6        | COMMIT 保存          | AA 55 20 0D 00 00 9A 27                                           | 0xA0；status=00；日志出现 commit |              |              | 写入 NV。              |
| 7        | REBOOT 重启          | AA 55 21 0E 00 00 6B DB                                           | 0xA1；status=00；设备重启        |              |              | 等待启动完成。         |
| 8        | 重启后读设备信息     | AA 55 01 01 00 00 50 18                                           | role 仍为 ROOT                   |              |              | 确认角色保留。         |
| 9        | 重启后读串口配置     | AA 55 02 02 00 00 A0 5C                                           | 返回保存的串口参数               |              |              | 确认串口配置保留。     |
| 10       | 重启后读 Modbus 配置 | AA 55 03 03 00 00 F0 60                                           | 返回 2 项 Modbus 预设            |              |              | 确认 Modbus 配置保留。 |
| 11       | 重启后读白名单       | AA 55 04 04 00 00 40 D5                                           | 能读到 A1:A2:A3:A4:A5:A6                   |              |              | 确认白名单保留。       |
| 12       | 重启后读功率         | AA 55 05 05 00 00 10 E9                                           | 返回 power=5                     |              |              | 确认功率保留。         |

# 8. 运行模式拒配测试表

| **步骤** | **测试项**        | **发送 Hex**                                                      | **期望结果**                | **实际响应 Hex** | **是否通过** | **备注**                 |
|----------|-------------------|-------------------------------------------------------------------|-----------------------------|------------------|--------------|--------------------------|
| 1        | GPIO13 置低并上电 | \-                                                                | 进入 RUN 模式               |                  |              | 模式由拨码决定。         |
| 2        | GET_MODE_STATUS   | AA 55 06 06 00 00 E0 AD                                           | status=00；current_mode=RUN |                  |              | 运行模式允许。           |
| 3        | READ_DEV_INFO     | AA 55 01 01 00 00 50 18                                           | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝配置类读取。 |
| 4        | READ_UART_CFG     | AA 55 02 02 00 00 A0 5C                                           | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 5        | READ_MODBUS_CFG   | AA 55 03 03 00 00 F0 60                                           | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 6        | READ_ROOT_WL_ALL  | AA 55 04 04 00 00 40 D5                                           | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 7        | READ_ROOT_POWER   | AA 55 05 05 00 00 10 E9                                           | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 8        | SET_ROLE          | AA 55 10 06 01 00 01 75 4B                                        | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 9        | SET_UART_CFG      | AA 55 11 07 04 00 07 00 01 08 F6 AD                               | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 10       | SET_MODBUS_CFG    | AA 55 12 08 05 00 02 01 02 05 03 10 5B                            | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 11       | SET_ROOT_POWER    | AA 55 13 09 01 00 05 33 9C                                        | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 12       | ADD_WL_ITEM       | AA 55 14 0A 06 00 A1 A2 A3 A4 A5 A6 B6 E8 | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 13       | DEL_WL_ITEM       | AA 55 15 0B 06 00 A1 A2 A3 A4 A5 A6 EA BD                         | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 14       | CLEAR_WL          | AA 55 16 0C 00 00 C4 6F                                           | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 15       | COMMIT            | AA 55 20 0D 00 00 9A 27                                           | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 16       | FACTORY_RESET     | AA 55 22 0F 00 00 3A 5F                                           | status=05 NOT_CONFIG        |                  |              | 运行模式拒绝。           |
| 17       | REBOOT            | AA 55 21 0E 00 00 6B DB                                           | status=00；设备重启         |                  |              | 运行模式允许。           |

# 9. 恢复出厂测试表

| **步骤** | **操作**                | **发送 Hex**                                           | **期望结果**                     | **实际结果** | **是否通过** |
|----------|-------------------------|--------------------------------------------------------|----------------------------------|--------------|--------------|
| 1        | 先修改多个配置并 COMMIT | 参考第 7 节                                            | 确认配置已改变并保存             |              |              |
| 2        | FACTORY_RESET           | AA 55 22 0F 00 00 3A 5F                                | 0xA2；status=00                  |              |              |
| 3        | READ_DEV_INFO           | AA 55 01 01 00 00 50 18                                | 角色恢复默认，当前默认 role=NODE |              |              |
| 4        | READ_UART_CFG           | AA 55 02 02 00 00 A0 5C                                | 恢复默认 115200,None,1,8         |              |              |
| 5        | READ_MODBUS_CFG         | AA 55 03 03 00 00 F0 60                                | modbus_count=0                   |              |              |
| 6        | 切 ROOT 后读 ROOT_POWER | AA 55 13 09 01 00 05 33 9C；再 AA 55 05 05 00 00 10 E9 | power 恢复默认 5                 |              |              |
| 7        | 读白名单                | AA 55 04 04 00 00 40 D5                                | wl_count=0                       |              |              |

# 10. UART / BLE 一致性测试表

| **步骤** | **通道** | **操作**           | **发送/设置**                               | **期望结果**                           | **实际结果** | **是否通过** |
|----------|----------|--------------------|---------------------------------------------|----------------------------------------|--------------|--------------|
| 1        | BLE      | 连接设备           | 发现 Service 0xFDF0 / Characteristic 0xFDF1 | 能连接并发现特征值                     |              |              |
| 2        | BLE      | 打开 Notify        | 写 CCCD(0x2902)=0x0001                      | Notify 成功开启                        |              |              |
| 3        | UART     | 发送 READ_UART_CFG | AA 55 02 02 00 00 A0 5C                     | 收到 status=00 与串口配置字段          |              |              |
| 4        | BLE      | 发送 READ_UART_CFG | AA 55 02 02 00 00 A0 5C                     | 收到与 UART 一致的业务 BODY            |              |              |
| 5        | BLE      | 不开 Notify 反测   | 关闭 Notify 后写任意读命令                  | 请求可能执行，但客户端收不到响应       |              |              |
| 6        | UART/BLE | 对比同一条读命令   | 任选 READ_DEV_INFO / READ_MODBUS_CFG        | 两边返回的业务内容一致，响应走各自通道 |              |              |

# 11. 日志核对表

| **日志关键字/现象**  | **含义**           | **需要检查什么**                  | **是否正常** |
|----------------------|--------------------|-----------------------------------|--------------|
| DTU rx len=          | 收到完整请求帧     | 请求是否进来了，长度是否正确      |              |
| DTU rx detail:       | 请求解析结果       | cmd/seq/len 是否正确              |              |
| DTU body detail:     | 请求 BODY 解释     | 参数字段是否正确                  |              |
| DTU frame crc check: | CRC 校验结果       | calc_crc 是否等于 recv_crc        |              |
| DTU tx len=          | 实际回发的二进制帧 | 是否有响应，长度是否合理          |              |
| DTU tx detail:       | 响应命令解释       | 响应 CMD 是否等于请求 CMD \| 0x80 |              |
| DTU tx body\[0\]=... | 响应状态码         | SUCC 还是错误码                   |              |
| DTU commit ...       | COMMIT 执行        | 是否写入 NV，是否有 snapshot 日志 |              |
| DTU boot config ...  | 启动配置加载       | 重启后是否按保存配置恢复          |              |
| ... reject ...       | 运行模式拒配       | RUN 模式下配置类命令是否被拒绝    |              |
| \[BLE dtu server\]   | BLE 通道日志       | BLE 写入、Notify 回包是否正常     |              |

# 12. 最终验收清单

| **序号** | **验收项**                 | **通过标准**                                                                                                    | **是否通过** | **备注** |
|----------|----------------------------|-----------------------------------------------------------------------------------------------------------------|--------------|----------|
| 1        | 配置模式全部读取命令通过   | READ_DEV_INFO / READ_UART_CFG / READ_MODBUS_CFG / READ_ROOT_WL_ALL / READ_ROOT_POWER / GET_MODE_STATUS 符合预期 |              |          |
| 2        | 配置模式全部写入命令通过   | SET_ROLE / SET_UART_CFG / SET_MODBUS_CFG / SET_ROOT_POWER / ADD/DEL/CLEAR_WL 均符合预期                         |              |          |
| 3        | ROOT 相关命令角色限制正确  | NODE 下 ROOT 专属命令返回 ROLE_MISMATCH                                                                         |              |          |
| 4        | COMMIT + REBOOT 持久化正确 | 重启后角色、串口、Modbus、功率、白名单保留                                                                      |              |          |
| 5        | 运行模式拒配正确           | 除 GET_MODE_STATUS 和 REBOOT 外，配置类命令返回 NOT_CONFIG                                                      |              |          |
| 6        | FACTORY_RESET 正确         | 配置恢复默认：NODE、115200 8N1、modbus_count=0、power=5、wl_count=0                                             |              |          |
| 7        | UART 通道可用              | 串口收发稳定，CRC 校验通过                                                                                      |              |          |
| 8        | BLE 通道可用               | 打开 Notify 后可正常收到响应                                                                                    |              |          |
| 9        | UART/BLE 业务结果一致      | 同一请求两通道返回业务 BODY 一致                                                                                |              |          |

# 13. 通用结果记录模板

| **测试项**      | **发送 Hex**                                                      | **实际响应 Hex** | **期望结果**                  | **实际结果** | **是否通过** | **备注** |
|-----------------|-------------------------------------------------------------------|------------------|-------------------------------|--------------|--------------|----------|
| READ_DEV_INFO   | AA 55 01 01 00 00 50 18                                           |                  | 返回 role/mac/name（设备自身 name，非白名单 name），status=00 |              |              |          |
| READ_UART_CFG   | AA 55 02 02 00 00 A0 5C                                           |                  | 返回 115200,None,1,8          |              |              |          |
| READ_MODBUS_CFG | AA 55 03 03 00 00 F0 60                                           |                  | 返回空或已配置项              |              |              |          |
| GET_MODE_STATUS | AA 55 06 06 00 00 E0 AD                                           |                  | 返回当前模式/角色/串口状态    |              |              |          |
| SET_ROLE(ROOT)  | AA 55 10 06 01 00 01 75 4B                                        |                  | status=00                     |              |              |          |
| ADD_WL_ITEM     | AA 55 14 0A 06 00 A1 A2 A3 A4 A5 A6 B6 E8 |                  | status=00                     |              |              |          |
| COMMIT          | AA 55 20 0D 00 00 9A 27                                           |                  | status=00                     |              |              |          |
| REBOOT          | AA 55 21 0E 00 00 6B DB                                           |                  | status=00 后重启              |              |              |          |

整理备注：控制类 COMMIT/REBOOT/FACTORY_RESET 采用完整测试流程中的统一
SEQ 与 CRC；如测试脚本自动递增 SEQ，请重新计算 CRC。

# 14. Version 2 白名单协议变更说明

## 14.1 变更目标

V2 版本彻底去掉白名单读写中的 `name_len` 和 `name` 字段，白名单只保存和传输子设备 MAC。

注意：`READ_DEV_INFO` 中的设备自身 `name_len + name` 不变；本次只修改 ROOT 白名单相关命令。

## 14.2 变更范围

| 项目 | V1 | V2 |
|------|----|----|
| ADD_WL_ITEM 请求 BODY | `mac(6) + name_len(1) + name(N)` | `mac(6)` |
| READ_ROOT_WL_ALL item | `mac(6) + name_len(1) + name(N)` | `mac(6)` |
| DEL_WL_ITEM 请求 BODY | `mac(6)` | 不变 |
| CLEAR_WL 请求 BODY | 无 | 不变 |
| READ_DEV_INFO 响应 BODY | `status + role + mac + name_len + name` | 不变 |

## 14.3 V2 白名单 BODY 结构

### ADD_WL_ITEM 请求 BODY

| 字段 | 长度 | 说明 |
|------|------|------|
| mac | 6 | 子设备 MAC |

### READ_ROOT_WL_ALL 响应 BODY

| 字段 | 长度 | 说明 |
|------|------|------|
| status | 1 | 状态码 |
| frag_idx | 1 | 当前分片序号，从 1 开始 |
| frag_total | 1 | 总分片数 |
| wl_total | 1 | 白名单总条数 |
| item_count | 1 | 当前分片内白名单条数 |
| item[n] | `6 * item_count` | 每个 item 只包含 `mac(6)` |

### 白名单 item

| 字段 | 长度 | 说明 |
|------|------|------|
| mac | 6 | 子设备 MAC |

## 14.4 固定示例更新

### 新增白名单

V1：

```plain
AA 55 14 0A 0E 00 A1 A2 A3 A4 A5 A6 07 44 54 55 5F 4E 30 31 07 7B
```

V2：

```plain
AA 55 14 0A 06 00 A1 A2 A3 A4 A5 A6 B6 E8
```

BODY 解析：

| 字节 | 含义 |
|------|------|
| `A1 A2 A3 A4 A5 A6` | 子设备 MAC |

## 14.5 测试判断变化

V2 后，白名单读回时不再判断 name，只判断 MAC 是否存在、数量是否正确、分片是否完整。

例如读取 1 条白名单时，期望：

```plain
status=00
frag_idx=01
frag_total=01
wl_total=01
item_count=01
item[0].mac=A1 A2 A3 A4 A5 A6
```

## 14.6 对测试脚本的要求

1. `ADD_WL_ITEM` 构造 BODY 时只拼接 6 字节 MAC。
2. `READ_ROOT_WL_ALL` 解析 item 时每 6 字节解析一个 MAC。
3. 不再解析 `name_len` 和 `name`。
4. 判断白名单完整性时，只比较 MAC 集合和 `wl_total`。
5. 原先用于生成 `WL001`、`WL002` 的 name 逻辑应删除或只作为脚本本地显示用途，不进入协议帧。

## 14.7 对固件测试的要求

1. ROOT 下 `ADD_WL_ITEM(mac)` 返回 `status=00`。
2. 重复添加同一个 MAC 时，按当前固件策略返回成功或已存在错误；但不能破坏白名单数量。
3. `READ_ROOT_WL_ALL` 返回的每个 item 固定 6 字节。
4. `DEL_WL_ITEM(mac)` 能删除指定 MAC。
5. `CLEAR_WL` 后 `READ_ROOT_WL_ALL` 返回 `wl_total=0`。
6. `COMMIT + REBOOT` 后白名单 MAC 仍能读回。
7. `FACTORY_RESET` 后白名单为空。

