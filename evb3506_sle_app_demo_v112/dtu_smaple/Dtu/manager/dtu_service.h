/**
 * @file dtu_service.h
 * @brief DTU服务管理器对外接口
 * @details 本头文件定义了DTU服务管理器的对外接口，包括：
 *          - 初始化总入口
 *          - transport输入入口
 *          - trace统计接口
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 使用说明：
 *       - 其他模块通过本头文件调用服务管理器的功能
 *       - 服务管理器负责初始化顺序和输入分流
 *       - 不直接访问服务管理器的内部数据
 */

#ifndef DTU_SERVICE_H
#define DTU_SERVICE_H

#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"

/* DTU manager：初始化总入口和 transport 输入入口。 */

/**
 * @brief DTU服务初始化
 * @details 初始化DTU系统的所有模块，包括storage、board、uart、ble/sle
 *
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @note 该函数是DTU系统的总初始化入口，由dtu_init_task调用
 */
errcode_t dtu_service_init(void);

/**
 * @brief 处理接收到的字节数据
 * @details 根据当前模式和传输通道ID将数据分发到对应的处理模块
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] data 接收到的数据指针
 * @param[in] len 数据长度
 */
void dtu_service_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len);

/**
 * @brief 处理UART1/485接收到的字节数据
 * @details 在RUN模式下处理485总线接收到的数据
 *
 * @param[in] data 接收到的数据指针
 * @param[in] len 数据长度
 */
void dtu_service_on_uart485_bytes(const uint8_t *data, uint16_t len);

/**
 * @brief 累计接收批量统计
 * @details 在trace打开时统计接收数据的批量信息
 *
 * @param[in] length 原始数据长度
 * @param[in] accepted 实际接收数据长度
 * @param[in] ring_used 环形缓冲区已使用大小
 */
void dtu_service_trace_rx_batch(uint16_t length, uint16_t accepted, uint16_t ring_used);

/**
 * @brief 累计解析任务唤醒次数
 * @details 在trace打开时统计任务唤醒次数
 */
void dtu_service_trace_rx_task_wakeup(void);

#endif
