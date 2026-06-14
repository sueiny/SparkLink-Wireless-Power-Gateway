#ifndef SLE_CMD_HANDLER_H
#define SLE_CMD_HANDLER_H

/*
 * SLE 命令处理器
 *
 * 职责：
 *   接收来自 gatewayd 的命令请求，查找目标 DTU 的 SLE 连接，
 *   构建 Modbus 写请求并通过 ssapc_write_req() 发送到设备。
 *
 * 当前阶段：
 *   Mock 模式 — 不真正调用 SLE SDK，返回模拟成功。
 *   后续接入真实 SLE 时只需修改 handler 内部实现。
 */

#include "ipc_cmd_protocol.h"
#include <stdint.h>

/*
 * 命令处理器初始化。
 * 返回 0 成功，非 0 失败。
 */
int sle_cmd_handler_init(void);

/*
 * 命令处理器反初始化。
 */
void sle_cmd_handler_deinit(void);

/*
 * 命令处理回调（供 ipc_cmd_receiver 使用）。
 * req: 收到的命令请求。
 * resp_data: 响应数据缓冲区。
 * resp_data_len: 输入缓冲区大小，输出实际数据长度。
 * 返回: CMD_RESULT_* 响应码。
 */
uint8_t sle_cmd_handler_process(const ipc_cmd_request_t *req,
                                uint8_t *resp_data, uint16_t *resp_data_len);

#endif /* SLE_CMD_HANDLER_H */
