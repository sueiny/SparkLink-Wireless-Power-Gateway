/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE mesh frame encode/decode.
 */
#include "SM_mesh_internal.h"

#include <string.h>
#include "securec.h"

/*--------------------------------------------------------------------------
 * Byte-order helpers / 字节序辅助
 *--------------------------------------------------------------------------*/

uint16_t sm_get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

void sm_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/*--------------------------------------------------------------------------
 * Frame parse / 帧解析
 *--------------------------------------------------------------------------*/

bool sm_parse_frame(const uint8_t *data, uint16_t data_len, sm_frame_view_t *view)
{
    if (data == NULL || view == NULL || data_len < 5) {
        return false;
    }
    if (data[0] != SM_MAGIC0 || data[1] != SM_MAGIC1 || data[2] != SM_PROTO_VERSION) {
        return false;
    }
    (void)memset_s(view, sizeof(*view), 0, sizeof(*view));
    view->frame_type = data[3];
    view->src_addr = data[4];

    if (view->frame_type == SM_FRAME_TYPE_RSSI_REPORT) {
        if (data_len < 6) return false;
        view->count = data[5];
        view->payload = &data[6];
        view->payload_len = data_len - 6;
        return true;
    }

    if (view->frame_type == SM_FRAME_TYPE_PATH_REQ) {
        if (data_len < 8) return false;
        view->dst_addr = data[5];
        view->seq = sm_get_le16(&data[6]);
        return true;
    }

    if (view->frame_type == SM_FRAME_TYPE_PATH_RESP) {
        if (data_len < 9) return false;
        view->dst_addr = data[5];
        view->seq = sm_get_le16(&data[6]);
        view->path_len = data[8];
        if (data_len < (uint16_t)(9 + view->path_len)) return false;
        view->path = &data[9];
        return true;
    }

    if (view->frame_type == SM_FRAME_TYPE_ACK) {
        if (data_len < 8) return false;
        view->dst_addr = data[5];
        view->seq = sm_get_le16(&data[6]);
        return true;
    }

    if (view->frame_type == SM_FRAME_TYPE_DATA) {
        if (data_len < 10) return false;
        view->dst_addr = data[5];
        view->seq = sm_get_le16(&data[6]);
        view->path_len = data[8];
        view->current_hop = data[9];
        if (data_len < (uint16_t)(10 + view->path_len)) return false;
        view->path = &data[10];
        uint16_t hdr_end = 10 + view->path_len;
        if (data_len > hdr_end) {
            view->payload = &data[hdr_end];
            view->payload_len = data_len - hdr_end;
        }
        return true;
    }

    return false;
}

/*--------------------------------------------------------------------------
 * Frame builders / 帧构建
 *--------------------------------------------------------------------------*/

uint16_t sm_build_rssi_report(uint8_t *buf, uint16_t max_len, uint8_t src_addr)
{
    uint8_t entries[SM_MAX_NEIGHBORS * 2];
    uint8_t count;
    uint16_t len;

    count = sm_get_neighbor_report(entries, sizeof(entries));
    len = 6 + (uint16_t)count * 2;
    if (len > max_len) {
        return 0;
    }
    buf[0] = SM_MAGIC0;
    buf[1] = SM_MAGIC1;
    buf[2] = SM_PROTO_VERSION;
    buf[3] = (uint8_t)SM_FRAME_TYPE_RSSI_REPORT;
    buf[4] = src_addr;
    buf[5] = count;
    if (count > 0) {
        (void)memcpy_s(&buf[6], max_len - 6, entries, (uint16_t)count * 2);
    }
    return len;
}

uint16_t sm_build_path_req(uint8_t *buf, uint16_t max_len, uint8_t src_addr, uint8_t dst_addr, uint16_t seq)
{
    if (max_len < 8) return 0;
    buf[0] = SM_MAGIC0;
    buf[1] = SM_MAGIC1;
    buf[2] = SM_PROTO_VERSION;
    buf[3] = (uint8_t)SM_FRAME_TYPE_PATH_REQ;
    buf[4] = src_addr;
    buf[5] = dst_addr;
    sm_put_le16(&buf[6], seq);
    return 8;
}

uint16_t sm_build_path_resp(uint8_t *buf, uint16_t max_len, uint8_t src_addr, uint8_t dst_addr,
    uint16_t seq, const uint8_t *path, uint8_t path_len)
{
    uint16_t len = 9 + path_len;
    if (max_len < len || path_len == 0) return 0;
    buf[0] = SM_MAGIC0;
    buf[1] = SM_MAGIC1;
    buf[2] = SM_PROTO_VERSION;
    buf[3] = (uint8_t)SM_FRAME_TYPE_PATH_RESP;
    buf[4] = src_addr;
    buf[5] = dst_addr;
    sm_put_le16(&buf[6], seq);
    buf[8] = path_len;
    (void)memcpy_s(&buf[9], max_len - 9, path, path_len);
    return len;
}

uint16_t sm_build_data_frame(uint8_t *buf, uint16_t max_len, uint8_t src_addr, uint8_t dst_addr,
    uint16_t seq, const uint8_t *path, uint8_t path_len, uint8_t current_hop,
    const uint8_t *payload, uint16_t payload_len)
{
    uint16_t hdr_len = 10 + path_len;
    uint16_t total_len = hdr_len + payload_len;
    if (max_len < total_len || path_len == 0) return 0;
    buf[0] = SM_MAGIC0;
    buf[1] = SM_MAGIC1;
    buf[2] = SM_PROTO_VERSION;
    buf[3] = (uint8_t)SM_FRAME_TYPE_DATA;
    buf[4] = src_addr;
    buf[5] = dst_addr;
    sm_put_le16(&buf[6], seq);
    buf[8] = path_len;
    buf[9] = current_hop;
    (void)memcpy_s(&buf[10], max_len - 10, path, path_len);
    if (payload != NULL && payload_len > 0) {
        (void)memcpy_s(&buf[hdr_len], max_len - hdr_len, payload, payload_len);
    }
    return total_len;
}

uint16_t sm_build_ack(uint8_t *buf, uint16_t max_len, uint8_t src_addr, uint16_t seq)
{
    if (max_len < 8) return 0;
    buf[0] = SM_MAGIC0;
    buf[1] = SM_MAGIC1;
    buf[2] = SM_PROTO_VERSION;
    buf[3] = (uint8_t)SM_FRAME_TYPE_ACK;
    buf[4] = src_addr;
    buf[5] = SM_DONGLE_ADDR;
    sm_put_le16(&buf[6], seq);
    return 8;
}
