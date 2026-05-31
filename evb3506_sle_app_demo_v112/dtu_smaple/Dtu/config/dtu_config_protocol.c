#include "dtu_config.h"

#include <stdbool.h>

#include "dtu_log.h"
#include "dtu_service_internal.h"
#include "errcode.h"
#include "securec.h"

/* ==================== AA55 parser / CRC / response pack ==================== */
/* 本文件只做协议算法，不碰 storage，也不执行命令。
 * 输入：transport 提交的原始字节流。
 * 输出：dtu_frame_t 或 CRC/LEN 错误状态。
 */
typedef enum {
    DTU_PARSE_SOF0 = 0,
    DTU_PARSE_SOF1,
    DTU_PARSE_CMD,
    DTU_PARSE_SEQ,
    DTU_PARSE_LEN0,
    DTU_PARSE_LEN1,
    DTU_PARSE_BODY,
    DTU_PARSE_CRC0,
    DTU_PARSE_CRC1,
    DTU_PARSE_SKIP
} dtu_parse_state_t;

typedef struct {
    dtu_parse_state_t state;
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint16_t body_pos;
    uint16_t crc_recv;
    uint16_t skip_left;
    uint8_t body[DTU_CFG_MAX_FRAME_BODY];
} dtu_protocol_parser_t;

static dtu_protocol_parser_t g_dtu_config_parsers[DTU_TRANSPORT_MAX];
static bool g_dtu_config_parser_ready[DTU_TRANSPORT_MAX];

/* 协议字段统一小端写入：len 和 crc 都按 low byte -> high byte。 */
static void dtu_config_append_u16_le(uint8_t *buf, uint16_t *offset, uint16_t value)
{
    buf[*offset] = (uint8_t)(value & 0xFF);
    buf[*offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    *offset = (uint16_t)(*offset + 2);
}

static void dtu_config_parser_init(dtu_protocol_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }

    parser->state = DTU_PARSE_SOF0;
    parser->cmd = 0;
    parser->seq = 0;
    parser->len = 0;
    parser->body_pos = 0;
    parser->crc_recv = 0;
    parser->skip_left = 0;
}

static uint16_t dtu_config_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = ((crc & 0x0001U) != 0U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static void dtu_config_fill_frame(const dtu_protocol_parser_t *parser, dtu_frame_t *frame)
{
    frame->cmd = parser->cmd;
    frame->seq = parser->seq;
    frame->len = parser->len;
    if (parser->len > 0) {
        (void)memcpy_s(frame->body, sizeof(frame->body), parser->body, parser->len);
    }
}

static dtu_protocol_status_t dtu_config_feed_byte(dtu_protocol_parser_t *parser, uint8_t byte, dtu_frame_t *frame)
{
    if (parser == NULL || frame == NULL) {
        return DTU_PROTOCOL_STATUS_INVALID_SOF;
    }

    switch (parser->state) {
        case DTU_PARSE_SOF0:
            if (byte == DTU_CFG_SOF0) {
                parser->state = DTU_PARSE_SOF1;
            }
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_SOF1:
            if (byte == DTU_CFG_SOF1) {
                parser->state = DTU_PARSE_CMD;
                return DTU_PROTOCOL_STATUS_INCOMPLETE;
            }
            parser->state = (byte == DTU_CFG_SOF0) ? DTU_PARSE_SOF1 : DTU_PARSE_SOF0;
            return DTU_PROTOCOL_STATUS_INVALID_SOF;
        case DTU_PARSE_CMD:
            parser->cmd = byte;
            parser->state = DTU_PARSE_SEQ;
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_SEQ:
            parser->seq = byte;
            parser->state = DTU_PARSE_LEN0;
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_LEN0:
            parser->len = byte;
            parser->state = DTU_PARSE_LEN1;
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_LEN1:
            parser->len |= (uint16_t)(byte << 8);
            parser->body_pos = 0;
            if (parser->len > DTU_CFG_MAX_FRAME_BODY) {
                /* body 太长时不继续写 parser->body，而是跳过剩余 body+crc，最后返回 LEN_ERR。 */
                parser->skip_left = (uint16_t)(parser->len + 2);
                parser->state = DTU_PARSE_SKIP;
            } else if (parser->len == 0) {
                parser->state = DTU_PARSE_CRC0;
            } else {
                parser->state = DTU_PARSE_BODY;
            }
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_BODY:
            parser->body[parser->body_pos++] = byte;
            if (parser->body_pos >= parser->len) {
                parser->state = DTU_PARSE_CRC0;
            }
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_CRC0:
            parser->crc_recv = byte;
            parser->state = DTU_PARSE_CRC1;
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        case DTU_PARSE_CRC1: {
            uint8_t crc_input[4 + DTU_CFG_MAX_FRAME_BODY];
            uint16_t crc_len = 0;
            uint16_t calc_crc;
            uint16_t recv_crc;

            recv_crc = (uint16_t)(parser->crc_recv | (uint16_t)(byte << 8));
            crc_input[crc_len++] = parser->cmd;
            crc_input[crc_len++] = parser->seq;
            crc_input[crc_len++] = (uint8_t)(parser->len & 0xFF);
            crc_input[crc_len++] = (uint8_t)((parser->len >> 8) & 0xFF);
            if (parser->len > 0) {
                (void)memcpy_s(&crc_input[crc_len], sizeof(crc_input) - crc_len, parser->body, parser->len);
                crc_len = (uint16_t)(crc_len + parser->len);
            }

            /* 先把 cmd/seq/len/body 填到 frame；CRC 失败时也需要 cmd/seq 回错误状态。 */
            dtu_config_fill_frame(parser, frame);
            calc_crc = dtu_config_crc16(crc_input, crc_len);
            dtu_config_parser_init(parser);
            if (calc_crc != recv_crc) {
                dtu_log_error("rx crc failed: cmd=%s seq=0x%02X len=%u calc=0x%04X recv=0x%04X",
                    dtu_service_cmd_name(frame->cmd), frame->seq, frame->len, calc_crc, recv_crc);
                return DTU_PROTOCOL_STATUS_CRC_ERR;
            }
            return DTU_PROTOCOL_STATUS_OK;
        }
        case DTU_PARSE_SKIP:
            if (parser->skip_left > 0) {
                parser->skip_left--;
            }
            if (parser->skip_left == 0) {
                frame->cmd = parser->cmd;
                frame->seq = parser->seq;
                frame->len = 0;
                dtu_config_parser_init(parser);
                return DTU_PROTOCOL_STATUS_LEN_ERR;
            }
            return DTU_PROTOCOL_STATUS_INCOMPLETE;
        default:
            dtu_config_parser_init(parser);
            return DTU_PROTOCOL_STATUS_INVALID_SOF;
    }
}

static dtu_protocol_parser_t *dtu_config_get_parser(dtu_transport_id_t transport_id)
{
    if (!g_dtu_config_parser_ready[transport_id]) {
        dtu_config_parser_init(&g_dtu_config_parsers[transport_id]);
        g_dtu_config_parser_ready[transport_id] = true;
    }
    return &g_dtu_config_parsers[transport_id];
}

errcode_t dtu_config_pack_response(uint8_t cmd, uint8_t seq, const uint8_t *body, uint16_t body_len,
    uint8_t *out, uint16_t out_size, uint16_t *out_len)
{
    uint16_t offset = 0;
    uint16_t crc;

    if (out == NULL || out_len == NULL || body_len > DTU_CFG_MAX_FRAME_BODY) {
        return ERRCODE_FAIL;
    }
    if (body_len > 0 && body == NULL) {
        return ERRCODE_FAIL;
    }
    if (out_size < (uint16_t)(body_len + 8)) {
        return ERRCODE_FAIL;
    }

    /* 响应帧格式：AA 55 + (cmd|0x80) + seq + len_le + body + crc_le。 */
    out[offset++] = DTU_CFG_SOF0;
    out[offset++] = DTU_CFG_SOF1;
    out[offset++] = (uint8_t)(cmd | 0x80U);
    out[offset++] = seq;
    dtu_config_append_u16_le(out, &offset, body_len);
    if (body_len > 0 && body != NULL) {
        if (memcpy_s(&out[offset], out_size - offset, body, body_len) != EOK) {
            return ERRCODE_FAIL;
        }
        offset = (uint16_t)(offset + body_len);
    }

    crc = dtu_config_crc16(&out[2], (uint16_t)(4 + body_len));
    dtu_config_append_u16_le(out, &offset, crc);
    *out_len = offset;
    return ERRCODE_SUCC;
}

dtu_protocol_status_t dtu_config_protocol_feed_byte(dtu_transport_id_t transport_id, uint8_t byte,
    dtu_frame_t *frame)
{
    if (transport_id >= DTU_TRANSPORT_MAX) {
        return DTU_PROTOCOL_STATUS_INVALID_SOF;
    }
    return dtu_config_feed_byte(dtu_config_get_parser(transport_id), byte, frame);
}
