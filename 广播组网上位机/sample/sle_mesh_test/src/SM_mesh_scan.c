/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE mesh scan result handling and neighbor table management.
 */
#include "SM_mesh_internal.h"

#include <string.h>
#include "securec.h"
#include "systick.h"

/*--------------------------------------------------------------------------
 * Neighbor table management / 邻居表管理
 *--------------------------------------------------------------------------*/

void sm_update_neighbor(uint8_t addr, int8_t rssi)
{
    uint8_t i;
    uint8_t free_idx = SM_MAX_NEIGHBORS;
    uint32_t now_ms = (uint32_t)sm_now_ms();

    if (addr == SM_NODE_ADDR_INVALID || addr == g_sm_ctx.local_addr) {
        return;
    }

    for (i = 0; i < SM_MAX_NEIGHBORS; i++) {
        if (g_sm_ctx.neighbors[i].in_use && g_sm_ctx.neighbors[i].addr == addr) {
            g_sm_ctx.neighbors[i].rssi = rssi;
            g_sm_ctx.neighbors[i].last_seen_ms = now_ms;
            return;
        }
        if (!g_sm_ctx.neighbors[i].in_use && free_idx == SM_MAX_NEIGHBORS) {
            free_idx = i;
        }
    }

    if (free_idx < SM_MAX_NEIGHBORS) {
        g_sm_ctx.neighbors[free_idx].in_use = true;
        g_sm_ctx.neighbors[free_idx].addr = addr;
        g_sm_ctx.neighbors[free_idx].rssi = rssi;
        g_sm_ctx.neighbors[free_idx].last_seen_ms = now_ms;
    }
}

void sm_age_neighbors(void)
{
    uint8_t i;
    uint32_t now_ms = (uint32_t)sm_now_ms();

    for (i = 0; i < SM_MAX_NEIGHBORS; i++) {
        if (g_sm_ctx.neighbors[i].in_use &&
            (now_ms - g_sm_ctx.neighbors[i].last_seen_ms) > SM_NEIGHBOR_TIMEOUT_MS) {
            g_sm_ctx.neighbors[i].in_use = false;
        }
    }
}

uint8_t sm_get_neighbor_report(uint8_t *entries, uint16_t max_len)
{
    uint8_t i;
    uint8_t count = 0;
    uint16_t offset = 0;

    for (i = 0; i < SM_MAX_NEIGHBORS && offset + 2 <= max_len; i++) {
        if (g_sm_ctx.neighbors[i].in_use) {
            entries[offset++] = g_sm_ctx.neighbors[i].addr;
            entries[offset++] = (uint8_t)g_sm_ctx.neighbors[i].rssi;
            count++;
        }
    }
    return count;
}

/*--------------------------------------------------------------------------
 * Handle seek result from scan callback / 处理扫描结果
 *--------------------------------------------------------------------------*/

static const uint8_t sm_mesh_mac_prefix[SLE_ADDR_LEN - 1] = {0x11, 0x00, 0x00, 0x00, 0x00};

void sm_handle_seek_result(sle_seek_result_info_t *result)
{
    const uint8_t *mesh_payload = NULL;
    uint16_t mesh_len = 0;
    sm_frame_view_t frame;

    if (result == NULL || result->data == NULL || result->data_length == 0) {
        return;
    }

    /* MAC prefix filter: only accept mesh network devices / MAC 前缀过滤 */
    if (memcmp(result->addr.addr, sm_mesh_mac_prefix, SLE_ADDR_LEN - 1) != 0) {
        return;
    }

    if (!sm_parse_adv_data(result->data, result->data_length, &mesh_payload, &mesh_len)) {
        return;
    }

    if (!sm_parse_frame(mesh_payload, mesh_len, &frame)) {
        return;
    }

    /* Update neighbor with scan RSSI / 用扫描 RSSI 更新邻居 */
    sm_update_neighbor(frame.src_addr, (int8_t)result->rssi);

    /* Dispatch to role handler / 分发到角色处理 */
    if (g_sm_ctx.is_dongle) {
        sm_dongle_handle_frame(&frame, (int8_t)result->rssi);
    } else {
        sm_node_handle_frame(&frame, (int8_t)result->rssi);
    }
}
