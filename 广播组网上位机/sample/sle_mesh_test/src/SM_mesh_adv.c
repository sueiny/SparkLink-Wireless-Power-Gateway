/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE mesh advertising and scan management.
 */
#include "SM_mesh_internal.h"

#include <string.h>
#include "securec.h"
#include "sle_device_discovery.h"

/*--------------------------------------------------------------------------
 * Advertising data TLV helpers / 广播数据 TLV 辅助
 *--------------------------------------------------------------------------*/

#define SM_DATA_TYPE_DISCOVERY_LEVEL    0x01
#define SM_DATA_TYPE_ACCESS_MODE        0x02
#define SM_DATA_TYPE_COMPLETE_LOCAL_NAME 0x09
#define SM_DATA_TYPE_MANUFACTURER_SPECIFIC 0xFF
#define SM_DATA_TYPE_TX_POWER_LEVEL     0x0A

void sm_append_adv_field(uint8_t *buffer, uint16_t max_len, uint16_t *offset,
    uint8_t type, const uint8_t *value, uint8_t value_len)
{
    if (buffer == NULL || offset == NULL || value == NULL) {
        return;
    }
    if ((*offset + 2U + value_len) > max_len) {
        return;
    }
    buffer[(*offset)++] = type;
    buffer[(*offset)++] = value_len;
    (void)memcpy_s(&buffer[*offset], max_len - *offset, value, value_len);
    *offset = (uint16_t)(*offset + value_len);
}

/*--------------------------------------------------------------------------
 * Parse advertising data: extract mesh frame from manufacturer TLV
 * 解析广播数据：从厂商 TLV 中提取 mesh 帧
 *--------------------------------------------------------------------------*/

bool sm_parse_adv_data(const uint8_t *data, uint16_t data_len,
    const uint8_t **mesh_payload, uint16_t *mesh_len)
{
    uint16_t offset = 0;

    if (data == NULL || mesh_payload == NULL || mesh_len == NULL) {
        return false;
    }
    *mesh_payload = NULL;
    *mesh_len = 0;

    while ((offset + 2U) <= data_len) {
        uint8_t type = data[offset++];
        uint8_t field_len = data[offset++];
        const uint8_t *value;

        if (field_len == 0) {
            break;
        }
        if ((offset + field_len) > data_len) {
            break;
        }
        value = &data[offset];
        if (type == SM_DATA_TYPE_MANUFACTURER_SPECIFIC && field_len >= 5 &&
            value[0] == SM_MAGIC0 && value[1] == SM_MAGIC1 && value[2] == SM_PROTO_VERSION) {
            *mesh_payload = value;
            *mesh_len = field_len;
            return true;
        }
        offset = (uint16_t)(offset + field_len);
    }
    return false;
}

/*--------------------------------------------------------------------------
 * Build advertising data with mesh frame / 构造包含 mesh 帧的广播数据
 *--------------------------------------------------------------------------*/

void sm_build_adv_data_direct(uint8_t *adv_buf, uint16_t *adv_len, const uint8_t *frame, uint16_t frame_len)
{
    uint16_t offset = 0;
    uint8_t discovery_level = 0;
    uint8_t access_mode = 0;

    *adv_len = 0;
    (void)memset_s(adv_buf, SM_ADV_DATA_LEN_MAX, 0, SM_ADV_DATA_LEN_MAX);

    sm_append_adv_field(adv_buf, SM_ADV_DATA_LEN_MAX, &offset,
        SM_DATA_TYPE_DISCOVERY_LEVEL, &discovery_level, sizeof(discovery_level));
    sm_append_adv_field(adv_buf, SM_ADV_DATA_LEN_MAX, &offset,
        SM_DATA_TYPE_ACCESS_MODE, &access_mode, sizeof(access_mode));

    if (frame != NULL && frame_len > 0) {
        sm_append_adv_field(adv_buf, SM_ADV_DATA_LEN_MAX, &offset,
            SM_DATA_TYPE_MANUFACTURER_SPECIFIC, frame, (uint8_t)frame_len);
    }
    *adv_len = offset;
}

/*--------------------------------------------------------------------------
 * Refresh advertising with new frame content / 刷新广播内容
 *--------------------------------------------------------------------------*/

void sm_build_scan_rsp(uint8_t *buf, uint16_t *len)
{
    uint16_t offset = 0;
    uint8_t tx_power = 20;
    const char *name = g_sm_ctx.is_dongle ? "sle_mesh_dongle" : "sle_mesh_node";
    uint8_t name_len = (uint8_t)strlen(name);

    sm_append_adv_field(buf, SM_ADV_DATA_LEN_MAX, &offset,
        SM_DATA_TYPE_TX_POWER_LEVEL, &tx_power, sizeof(tx_power));
    sm_append_adv_field(buf, SM_ADV_DATA_LEN_MAX, &offset,
        SM_DATA_TYPE_COMPLETE_LOCAL_NAME, (const uint8_t *)name, name_len);
    *len = offset;
}

void sm_refresh_advertising(const uint8_t *frame, uint16_t frame_len)
{
    sle_announce_data_t announce_data = {0};
    uint8_t adv_buf[SM_ADV_DATA_LEN_MAX] = {0};
    uint8_t scan_rsp_buf[SM_ADV_DATA_LEN_MAX] = {0};
    uint16_t adv_len = 0;
    uint16_t scan_rsp_len = 0;

    if (!g_sm_ctx.sle_enabled) {
        return;
    }

    if (g_sm_ctx.announce_started) {
        (void)sle_stop_announce(SM_ADV_HANDLE);
        g_sm_ctx.announce_started = false;
    }

    sm_set_announce_params();
    sm_build_adv_data_direct(adv_buf, &adv_len, frame, frame_len);
    sm_build_scan_rsp(scan_rsp_buf, &scan_rsp_len);
    announce_data.announce_data = adv_buf;
    announce_data.announce_data_len = adv_len;
    announce_data.seek_rsp_data = scan_rsp_buf;
    announce_data.seek_rsp_data_len = scan_rsp_len;

    if (sle_set_announce_data(SM_ADV_HANDLE, &announce_data) == ERRCODE_SUCC &&
        sle_start_announce(SM_ADV_HANDLE) == ERRCODE_SUCC) {
        g_sm_ctx.announce_started = true;
    }
}

/*--------------------------------------------------------------------------
 * Set announce parameters / 设置广播参数
 *--------------------------------------------------------------------------*/

void sm_set_announce_params(void)
{
    sle_announce_param_t param = {0};

    param.announce_mode = SLE_ANNOUNCE_MODE_NONCONN_SCANABLE;
    param.announce_handle = SM_ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SM_ADV_CHANNEL_MAP;
    param.announce_interval_min = SM_ADV_INTERVAL_MIN;
    param.announce_interval_max = SM_ADV_INTERVAL_MAX;
    param.conn_interval_min = 0x14;
    param.conn_interval_max = 0x14;
    param.conn_max_latency = 0x1F3;
    param.conn_supervision_timeout = 0x1F4;
    param.announce_tx_power = 20;
    param.own_addr.type = 0;
    param.own_addr.addr[0] = 0x11;
    param.own_addr.addr[1] = 0x00;
    param.own_addr.addr[2] = 0x00;
    param.own_addr.addr[3] = 0x00;
    param.own_addr.addr[4] = 0x00;
    param.own_addr.addr[5] = g_sm_ctx.local_addr;
    (void)sle_set_announce_param(SM_ADV_HANDLE, &param);
}

/*--------------------------------------------------------------------------
 * Scan start/stop / 扫描启停
 *--------------------------------------------------------------------------*/

void sm_start_scan(void)
{
    sle_seek_param_t param = {0};

    param.own_addr_type = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 0;
    param.seek_interval[0] = SM_SCAN_INTERVAL;
    param.seek_window[0] = SM_SCAN_WINDOW;

    if (sle_set_seek_param(&param) == ERRCODE_SUCC) {
        (void)sle_start_seek();
    }
}

void sm_stop_scan(void)
{
    (void)sle_stop_seek();
}
