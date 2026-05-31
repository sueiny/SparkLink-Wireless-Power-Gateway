/**
 * @file dtu_transport.h
 * @brief DTU传输通道抽象层接口
 * @details 本头文件定义了DTU传输通道的抽象接口，包括：
 *          - 传输通道接口结构体定义
 *          - 各传输通道的全局接口对象声明
 *          - UART子通道发送接口
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 架构说明：
 *       - 传输通道抽象层为上层提供统一的init/send接口
 *       - manager只调用init/send，不关心UART DMA、BLE notify、SLE notify的细节
 *       - 每个传输通道实现自己的init和send函数
 *
 * @par 支持的传输通道：
 *       - UART：串口传输，包括UART0（配置口）和UART1/485（业务口）
 *       - BLE：蓝牙低功耗传输，用于CONFIG模式下的无线配置
 *       - SLE：SLE传输，用于RUN模式下的组网和透明桥接
 */

#ifndef DTU_TRANSPORT_H
#define DTU_TRANSPORT_H

#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"

/**
 * @brief 传输通道接口结构体
 * @details 定义了传输通道的通用接口，每个传输通道需要实现这些接口
 *
 * @par 接口说明：
 *       - name：传输通道名称，用于日志输出
 *       - init：初始化函数，返回ERRCODE_SUCC表示成功
 *       - send：发送函数，返回ERRCODE_SUCC表示成功
 */
typedef struct {
    const char *name;
    errcode_t (*init)(void);
    errcode_t (*send)(const uint8_t *data, uint16_t len);
} dtu_transport_if_t;

/** @brief UART传输通道接口对象 */
extern const dtu_transport_if_t g_dtu_uart_transport;
/** @brief BLE传输通道接口对象 */
extern const dtu_transport_if_t g_dtu_ble_transport;
/** @brief SLE传输通道接口对象 */
extern const dtu_transport_if_t g_dtu_sle_transport;

/**
 * @brief UART transport 内部子通道：UART0 用于 PC 观察，UART1 用于 485 总线。
 * @details 这些函数用于RUN模式下的数据转发
 *
 * @param[in] data 要发送的数据指针
 * @param[in] len 数据长度
 * @return ERRCODE_SUCC成功，其他失败
 */
errcode_t dtu_uart_send_to_pc(const uint8_t *data, uint16_t len);
errcode_t dtu_uart_send_to_485(const uint8_t *data, uint16_t len);

#endif
