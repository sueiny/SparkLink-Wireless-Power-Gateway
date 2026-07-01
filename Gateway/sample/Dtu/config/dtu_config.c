/**
 * @file dtu_config.c
 * @brief CONFIG 协议 facade，负责 AA55 解析入口和命令分发。
 */

#include "dtu_config.h"

#include "dtu_service_internal.h"
#include "dtu_storage.h"

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
        dtu_protocol_status_t status = dtu_config_protocol_feed_byte(transport_id, data[i], &frame); // 每字节推进对应 transport 的 parser 状态机。

        switch (status) {
            case DTU_PROTOCOL_STATUS_OK:
                /* 只有完整帧且 CRC 正确才进入命令层。 */
                dtu_config_dispatch(transport_id, &frame); // 完整且 CRC 正确的帧进入命令表。
                break;
            case DTU_PROTOCOL_STATUS_CRC_ERR:
                /* CRC/LEN 错误在 facade 直接回状态码，不让命令 handler 看到坏帧。 */
                dtu_service_reply_status(transport_id, frame.cmd, frame.seq, DTU_CFG_STATUS_CRC_ERR); // 坏帧不下发 handler，直接回 CRC 错。
                break;
            case DTU_PROTOCOL_STATUS_LEN_ERR:
                dtu_service_reply_status(transport_id, frame.cmd, frame.seq, DTU_CFG_STATUS_LEN_ERR); // body 超限或长度异常直接回 LEN 错。
                break;
            case DTU_PROTOCOL_STATUS_INCOMPLETE:
            case DTU_PROTOCOL_STATUS_INVALID_SOF:
            default:
                break;
        }
    }
}

void dtu_config_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    /* REBOOT 命令置位后冻结后续配置写入，等待 UART/BLE/SLE task 在安全点真正复位。 */
    if (dtu_storage_is_reboot_pending()) { // REBOOT 已受理后冻结后续配置写入。
        dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_BUSY);
        return;
    }

    if (dtu_config_commands_dispatch(transport_id, frame)) { // 命令命中后由具体 handler 负责回包。
        return;
    }

    dtu_service_reply_status(transport_id, frame->cmd, frame->seq, DTU_CFG_STATUS_CMD_ERR); // 未注册命令统一回 CMD_ERR。
}
