/**
 * @file dtu_config.c
 * @brief DTU配置协议处理模块
 * @details 本模块负责DTU配置协议的处理，主要职责：
 *          1. 承接manager输入，调用AA55配置协议解析
 *          2. 将完整帧交给配置命令表
 *          3. 通过manager的统一发送出口回复，不直接关心UART/BLE/SLE细节
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 架构说明：
 *       - 配置协议处理模块是DTU系统的配置核心
 *       - 负责解析AA55格式的配置协议帧
 *       - 将解析后的帧分发到对应的命令处理函数
 *       - 通过manager的统一接口发送响应
 *
 * @par 协议处理流程：
 *       1. 接收原始字节数据
 *       2. 逐字节喂给AA55状态机
 *       3. 状态机返回完整帧或错误状态
 *       4. 完整帧交给命令分发函数处理
 *       5. 错误帧直接返回错误状态码
 *
 * @par 错误处理：
 *       - CRC错误：返回DTU_CFG_STATUS_CRC_ERR
 *       - 长度错误：返回DTU_CFG_STATUS_LEN_ERR
 *       - 命令错误：返回DTU_CFG_STATUS_CMD_ERR
 *       - 参数错误：返回DTU_CFG_STATUS_PARAM_ERR
 */

#include "dtu_config.h"

#include "dtu_service_internal.h"
#include "dtu_storage.h"

/* config facade 职责：
 * 1. 承接 manager 输入，调用 AA55 配置协议解析。
 * 2. 将完整帧交给配置命令表。
 * 3. 通过 manager 的统一发送出口回复，不直接关心 UART/BLE/SLE 细节。
 */

/**
 * @brief 处理接收到的字节数据
 * @details 将接收到的字节数据逐字节喂给AA55协议状态机
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] data 接收到的数据指针
 * @param[in] len 数据长度
 *
 * @par 处理逻辑：
 *       - 每个transport都有独立parser状态
 *       - transport task可以一次提交多个字节
 *       - 逐字节喂给AA55状态机
 *       - 根据状态机返回状态进行相应处理
 */
void dtu_config_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len)
{
    if (transport_id >= DTU_TRANSPORT_MAX || data == NULL || len == 0) {
        return;
    }

    /* 每个 transport 都有独立 parser 状态。
     * transport task 可以一次提交多个字节；这里逐字节喂给 AA55 状态机。
     */
    for (uint16_t i = 0; i < len; i++) {
        dtu_frame_t frame = {0};
        dtu_protocol_status_t status = dtu_config_protocol_feed_byte(transport_id, data[i], &frame);

        switch (status) {
            case DTU_PROTOCOL_STATUS_OK:
                /* 只有完整帧且 CRC 正确才进入命令层。 */
                dtu_config_dispatch(transport_id, &frame);
                break;
            case DTU_PROTOCOL_STATUS_CRC_ERR:
                /* CRC/LEN 错误在 facade 直接回状态码，不让命令 handler 看到坏帧。 */
                dtu_service_reply_status(transport_id, frame.cmd, frame.seq, DTU_CFG_STATUS_CRC_ERR);
                break;
            case DTU_PROTOCOL_STATUS_LEN_ERR:
                dtu_service_reply_status(transport_id, frame.cmd, frame.seq, DTU_CFG_STATUS_LEN_ERR);
                break;
            case DTU_PROTOCOL_STATUS_INCOMPLETE:
            case DTU_PROTOCOL_STATUS_INVALID_SOF:
            default:
                break;
        }
    }
}

/**
 * @brief 配置命令分发
 * @details 将完整的配置帧分发到对应的命令处理函数
 *
 * @param[in] transport_id 传输通道ID
 * @param[in] frame 接收到的完整帧
 *
 * @par 处理逻辑：
 *       1. 检查是否有待执行的重启命令
 *       2. 如果有待执行的重启，返回忙状态
 *       3. 调用配置命令分发表处理帧
 *       4. 如果命令未找到，返回命令错误状态
 */
void dtu_config_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    /* REBOOT 命令置位后冻结后续配置写入，等待 UART/BLE/SLE task 在安全点真正复位。 */
    if (dtu_storage_is_reboot_pending()) {
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_BUSY);
        return;
    }

    if (dtu_config_commands_dispatch(transport_id, frame)) {
        return;
    }

    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_CMD_ERR);
}
