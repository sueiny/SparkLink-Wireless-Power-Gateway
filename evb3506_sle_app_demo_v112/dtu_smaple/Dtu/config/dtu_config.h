/**
 * @file dtu_config.h
 * @brief DTU配置协议处理模块对外接口
 * @details 本头文件定义了DTU配置协议处理模块的对外接口，包括：
 *          - 配置协议帧结构体定义
 *          - 协议状态机状态枚举
 *          - 命令handler类型定义
 *          - 命令表结构定义
 *          - 模块对外接口函数声明
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 使用说明：
 *       - 其他模块通过本头文件调用配置协议处理功能
 *       - 配置协议处理模块负责AA55帧的解析和命令分发
 *       - 不直接访问配置协议处理模块的内部数据
 *
 * @par 协议帧格式：
 *       - SOF: AA 55（帧头）
 *       - CMD: 1字节（命令字）
 *       - SEQ: 1字节（序列号）
 *       - LEN: 2字节（数据长度）
 *       - DATA: N字节（数据体）
 *       - CRC: 2字节（校验和）
 */

#ifndef DTU_CONFIG_H
#define DTU_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"

/**
 * @brief 配置协议完整帧结构体
 * @details config内部AA55状态机解析成功后会产出这个结构
 *          命令层只处理结构化后的帧
 *
 * @par 字段说明：
 *       - cmd：命令字，定义在dtu_build_config.h中
 *       - seq：序列号，用于请求/响应匹配
 *       - len：数据体长度
 *       - body：数据体，最大长度为DTU_CFG_MAX_FRAME_BODY
 */
typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint8_t body[DTU_CFG_MAX_FRAME_BODY];
} dtu_frame_t;

/**
 * @brief 单字节状态机的返回状态
 * @details OK才能进入命令分发；CRC/LEN错误由config facade直接回状态码，不进入命令层
 *
 * @par 状态说明：
 *       - DTU_PROTOCOL_STATUS_INCOMPLETE：帧不完整，继续接收
 *       - DTU_PROTOCOL_STATUS_OK：帧完整且CRC正确
 *       - DTU_PROTOCOL_STATUS_CRC_ERR：CRC校验错误
 *       - DTU_PROTOCOL_STATUS_LEN_ERR：长度错误
 *       - DTU_PROTOCOL_STATUS_INVALID_SOF：无效的帧头
 */
typedef enum {
    DTU_PROTOCOL_STATUS_INCOMPLETE = 0,
    DTU_PROTOCOL_STATUS_OK,
    DTU_PROTOCOL_STATUS_CRC_ERR,
    DTU_PROTOCOL_STATUS_LEN_ERR,
    DTU_PROTOCOL_STATUS_INVALID_SOF
} dtu_protocol_status_t;

/**
 * @brief 配置命令handler统一签名
 * @details 所有配置命令处理函数都遵循这个签名
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] frame 接收到的完整帧
 */
typedef void (*dtu_cmd_handler_t)(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/**
 * @brief 表驱动命令项结构体
 * @details cmd_id命中后调用对应handler
 *
 * @par 字段说明：
 *       - cmd_id：命令字
 *       - handler：对应的处理函数
 */
typedef struct {
    uint8_t cmd_id;
    dtu_cmd_handler_t handler;
} dtu_cmd_entry_t;

/**
 * @brief 处理接收到的字节数据
 * @details 将接收到的字节数据逐字节喂给AA55协议状态机
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] data 接收到的数据指针
 * @param[in] len 数据长度
 */
void dtu_config_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len);

/**
 * @brief 配置命令分发
 * @details 将完整的配置帧分发到对应的命令处理函数
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] frame 接收到的完整帧
 */
void dtu_config_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/**
 * @brief 打包配置协议响应帧
 * @details 将响应数据打包成AA55格式的协议帧
 *
 * @param[in] cmd 命令字
 * @param[in] seq 序列号
 * @param[in] body 响应体数据指针
 * @param[in] body_len 响应体长度
 * @param[out] out 输出缓冲
 * @param[in] out_size 输出缓冲大小
 * @param[out] out_len 实际输出长度
 * @return ERRCODE_SUCC成功，其他失败
 */
errcode_t dtu_config_pack_response(uint8_t cmd, uint8_t seq, const uint8_t *body, uint16_t body_len,
    uint8_t *out, uint16_t out_size, uint16_t *out_len);

/* config 内部入口：
 * 只给 dtu_config.c / dtu_config_protocol.c / dtu_config_commands.c 互相调用。
 * 为了减少头文件数量，暂时收在同一个 config 头里，不单独拆 internal 头。
 */

/**
 * @brief 协议状态机字节输入
 * @details 将单个字节输入AA55协议状态机
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] byte 输入字节
 * @param[out] frame 解析完成的帧结构体
 * @return 协议状态机状态
 */
dtu_protocol_status_t dtu_config_protocol_feed_byte(dtu_transport_id_t transport_id, uint8_t byte,
    dtu_frame_t *frame);

/**
 * @brief 配置命令分发表
 * @details 在配置命令表中查找并执行handler
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] frame 接收到的完整帧
 * @return true: 命令已处理, false: 未找到匹配命令
 */
bool dtu_config_commands_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

#endif
