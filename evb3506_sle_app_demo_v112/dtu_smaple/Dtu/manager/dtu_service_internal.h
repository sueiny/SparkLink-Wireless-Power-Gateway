/**
 * @file dtu_service_internal.h
 * @brief DTU服务管理器内部接口
 * @details 本头文件定义了DTU服务管理器的内部接口，仅供manager目录下的文件使用
 *          包括transport访问、名称映射、命令分发、响应发送等功能
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @warning 本头文件为内部接口，不应被其他目录的文件包含
 *          外部模块应使用dtu_service.h中的公共接口
 */

#ifndef DTU_SERVICE_INTERNAL_H
#define DTU_SERVICE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "dtu_config.h"
#include "dtu_transport.h"

/**
 * @brief 获取传输通道接口对象
 * @details 根据传输通道ID获取对应的接口对象，供sender/init统一使用
 *
 * @param[in] transport_id 传输通道ID
 * @return 传输通道接口对象指针，未注册时返回NULL
 */
const dtu_transport_if_t *dtu_service_transport_if(dtu_transport_id_t transport_id);

/**
 * @brief 获取命令名称
 * @details 根据命令字返回对应的可读字符串，供日志使用
 *
 * @param[in] cmd 配置命令字
 * @return 命令名称字符串
 */
const char *dtu_service_cmd_name(uint8_t cmd);

/**
 * @brief 获取传输通道名称
 * @details 根据传输通道ID返回对应的可读字符串，供日志使用
 *
 * @param[in] transport_id 传输通道ID
 * @return 传输通道名称字符串
 */
const char *dtu_service_transport_name(dtu_transport_id_t transport_id);

/**
 * @brief 在命令表中查找并执行handler
 * @details 遍历命令表，查找与帧命令字匹配的条目并执行对应的处理函数
 *
 * @param[in] table 命令表数组
 * @param[in] table_size 命令表大小
 * @param[in] transport_id 传输通道ID
 * @param[in] frame 接收到的帧结构体
 * @return true: 命令已处理, false: 未找到匹配命令
 */
bool dtu_service_dispatch_table(const dtu_cmd_entry_t *table, uint32_t table_size,
    dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/**
 * @brief 发送完整配置协议响应帧
 * @details 通过指定的传输通道发送配置协议响应帧
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] cmd 命令字
 * @param[in] seq 序列号
 * @param[in] body 响应体数据指针
 * @param[in] body_len 响应体长度
 */
void dtu_service_reply(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq,
    const uint8_t *body, uint16_t body_len);

/**
 * @brief 发送仅状态码响应帧
 * @details 发送只包含状态码的响应帧，用于简单的成功/失败响应
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] cmd 命令字
 * @param[in] seq 序列号
 * @param[in] status 状态码
 */
void dtu_service_reply_status(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, uint8_t status);

#endif
