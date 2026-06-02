#ifndef GATEWAY_SLE_IPC_SENDER_H
#define GATEWAY_SLE_IPC_SENDER_H

#include <stdbool.h>
#include <stdint.h>

/* 初始化 IPC 发送端。非阻塞，首次发送时才真正连接。 */
int ipc_sender_init(void);

/* 关闭 IPC 连接。 */
void ipc_sender_deinit(void);

/*
 * 发送原始字节到 gatewayd。
 * data: 从 SLE notify 回调收到的完整数据（SLE 树网络帧）。
 * len: 数据长度。
 * 线程安全。gatewayd 直接从 socket 读取这些字节，无额外封装。
 */
bool ipc_sender_send_raw(const uint8_t *data, uint16_t len);

#endif
