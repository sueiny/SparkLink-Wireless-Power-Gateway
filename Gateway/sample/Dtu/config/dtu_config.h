/**
 * @file dtu_config.h
 * @brief CONFIG AA55 解析、命令分发和响应打包接口。
 */

#ifndef DTU_CONFIG_H
#define DTU_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"

/** AA55 状态机解析出的完整 CONFIG 帧。 */
typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint8_t body[DTU_CFG_MAX_FRAME_BODY];
} dtu_frame_t;

/** 解析结果；只有 OK 帧会进入命令分发。 */
typedef enum {
    DTU_PROTOCOL_STATUS_INCOMPLETE = 0,
    DTU_PROTOCOL_STATUS_OK,
    DTU_PROTOCOL_STATUS_CRC_ERR,
    DTU_PROTOCOL_STATUS_LEN_ERR,
    DTU_PROTOCOL_STATUS_INVALID_SOF
} dtu_protocol_status_t;

/** CONFIG 命令 handler 统一签名。 */
typedef void (*dtu_cmd_handler_t)(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/** 表驱动命令分发项。 */
typedef struct {
    uint8_t cmd_id;
    dtu_cmd_handler_t handler;
} dtu_cmd_entry_t;

/** 将 transport 收到的字节流送入 CONFIG parser。 */
void dtu_config_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len);

/** 将校验通过的 CONFIG 帧分发到命令表。 */
void dtu_config_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

/**
 * @brief 打包 AA55 响应帧。
 * @return ERRCODE_SUCC 表示成功。
 */
errcode_t dtu_config_pack_response(uint8_t cmd, uint8_t seq, const uint8_t *body, uint16_t body_len,
    uint8_t *out, uint16_t out_size, uint16_t *out_len);

/* config 内部入口：
 * 只给 dtu_config.c / dtu_config_protocol.c / dtu_config_commands.c 互相调用。
 * 为了减少头文件数量，暂时收在同一个 config 头里，不单独拆 internal 头。
 */

/** 内部单字节 AA55 parser 入口。 */
dtu_protocol_status_t dtu_config_protocol_feed_byte(dtu_transport_id_t transport_id, uint8_t byte,
    dtu_frame_t *frame);

/** 内部命令表分发入口。 */
bool dtu_config_commands_dispatch(dtu_transport_id_t transport_id, const dtu_frame_t *frame);

#endif
