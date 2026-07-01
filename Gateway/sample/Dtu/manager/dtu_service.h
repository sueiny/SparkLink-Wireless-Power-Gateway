/**
 * @file dtu_service.h
 * @brief DTU 初始化总入口、输入分流和 trace 统计接口。
 */

#ifndef DTU_SERVICE_H
#define DTU_SERVICE_H

#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"

/**
 * @brief 初始化 storage、board、UART，并按当前模式启动 BLE 或 SLE。
 * @return ERRCODE_SUCC 表示成功。
 */
errcode_t dtu_service_init(void);

/** 根据当前模式分发 UART/BLE/SLE 输入字节。 */
void dtu_service_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len);

/** RUN 模式下处理 UART1/485 返回数据。 */
void dtu_service_on_uart485_bytes(const uint8_t *data, uint16_t len);

/** trace 打开时累计 RX 批量和 ring 水位。 */
void dtu_service_trace_rx_batch(uint16_t length, uint16_t accepted, uint16_t ring_used);

/** trace 打开时累计解析任务唤醒次数。 */
void dtu_service_trace_rx_task_wakeup(void);

#endif
