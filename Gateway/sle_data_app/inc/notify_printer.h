#ifndef GATEWAY_SLE_NOTIFY_PRINTER_H
#define GATEWAY_SLE_NOTIFY_PRINTER_H

#include <stdbool.h>
#include <stdint.h>
#include "server_connections.h"

/* 启动 notify 日志 worker，清空并打开 /tmp/sle_app.log。 */
int notify_printer_start(void);

/* 停止 notify 日志 worker，drain 队列并关闭日志文件。 */
void notify_printer_stop(void);

/* 从 SLE 回调线程快速复制 notify/indication 数据并入队，不在回调线程打印。 */
bool notify_printer_enqueue_packet(int server_index, const sle_server_connection_t *conn,
    const uint8_t *data, uint16_t len);

#endif
