#ifndef IPC_CMD_RECEIVER_H
#define IPC_CMD_RECEIVER_H

/*
 * IPC 命令接收器
 * 作为 Unix Socket 服务端监听 cmd_socket，接收 gatewayd 发送的命令帧。
 * 独立线程运行，接收命令后调用回调处理并发送响应帧。
 * 协议：2 字节 LE 长度前缀 + 帧体（与数据通道一致）。
 */

#include "ipc_cmd_protocol.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * 命令处理回调。
 * req: 收到的命令请求。
 * resp_data: 响应数据缓冲区（由调用方填充）。
 * resp_data_len: 输入缓冲区大小，输出实际数据长度。
 * 返回: CMD_RESULT_* 响应码。
 */
typedef uint8_t (*cmd_handler_fn)(const ipc_cmd_request_t *req,
                                  uint8_t *resp_data, uint16_t *resp_data_len);

/*
 * 初始化命令接收器。
 * socket_path: Unix Socket 路径（抽象命名空间，首字节为 \0）。
 * path_len: 路径总长度（含首字节 \0）。
 * handler: 命令处理回调函数。
 * 返回 0 成功，非 0 失败。
 */
int ipc_cmd_receiver_init(const char *socket_path, int path_len, cmd_handler_fn handler);

/*
 * 停止并销毁命令接收器。
 * 会关闭 socket、终止线程、释放资源。
 */
void ipc_cmd_receiver_deinit(void);

/*
 * 命令接收器是否正在运行。
 */
bool ipc_cmd_receiver_is_running(void);

#endif /* IPC_CMD_RECEIVER_H */
