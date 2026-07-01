/**
 * @file dtu_transport.h
 * @brief UART、BLE、SLE 传输通道的统一 init/send 抽象。
 */

#ifndef DTU_TRANSPORT_H
#define DTU_TRANSPORT_H

#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"

/** transport 实现只暴露名称、初始化和发送入口。 */
typedef struct {
    const char *name;
    errcode_t (*init)(void);
    errcode_t (*send)(const uint8_t *data, uint16_t len);
} dtu_transport_if_t;

/** UART 传输通道接口对象。 */
extern const dtu_transport_if_t g_dtu_uart_transport;
/** BLE 传输通道接口对象。 */
extern const dtu_transport_if_t g_dtu_ble_transport;
/** SLE 传输通道接口对象。 */
extern const dtu_transport_if_t g_dtu_sle_transport;

/**
 * @brief RUN bridge 使用的 UART 子通道发送接口。
 * @return ERRCODE_SUCC 表示成功。
 */
errcode_t dtu_uart_send_to_pc(const uint8_t *data, uint16_t len);
errcode_t dtu_uart_send_to_485(const uint8_t *data, uint16_t len);

#endif
