/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE mesh normal node logic: RSSI report, relay, path request/response.
 */
#include "SM_mesh_internal.h"

#include <string.h>
#include "securec.h"
#include "systick.h"

/*--------------------------------------------------------------------------
 * Dedup table (relay loop prevention) / 去重表（防中继环路）
 *--------------------------------------------------------------------------*/

bool sm_dedup_check(uint8_t frame_type, uint8_t src_addr, uint16_t seq)
{
    uint8_t i;
    uint32_t now_ms = (uint32_t)sm_now_ms();

    for (i = 0; i < SM_DEDUP_TABLE_SIZE; i++) {
        sm_dedup_entry_t *e = &g_sm_ctx.dedup[i];
        if (e->in_use && e->frame_type == frame_type && e->src_addr == src_addr && e->seq == seq) {
            if ((now_ms - e->seen_ms) <= SM_DEDUP_TIMEOUT_MS) {
                return true;
            }
            e->in_use = false;
        }
    }
    return false;
}

void sm_dedup_add(uint8_t frame_type, uint8_t src_addr, uint16_t seq)
{
    uint8_t i;
    uint8_t free_idx = SM_DEDUP_TABLE_SIZE;
    uint32_t now_ms = (uint32_t)sm_now_ms();

    for (i = 0; i < SM_DEDUP_TABLE_SIZE; i++) {
        if (!g_sm_ctx.dedup[i].in_use) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < SM_DEDUP_TABLE_SIZE) {
        g_sm_ctx.dedup[free_idx].in_use = true;
        g_sm_ctx.dedup[free_idx].frame_type = frame_type;
        g_sm_ctx.dedup[free_idx].src_addr = src_addr;
        g_sm_ctx.dedup[free_idx].seq = seq;
        g_sm_ctx.dedup[free_idx].seen_ms = now_ms;
    }
}

/*--------------------------------------------------------------------------
 * Path cache / 路径缓存
 *--------------------------------------------------------------------------*/

void sm_store_path(uint8_t dst_addr, const uint8_t *path, uint8_t path_len)
{
    uint8_t i;
    uint8_t free_idx = SM_PATH_CACHE_SIZE;

    if (path_len > SLE_ADDR_LEN || path == NULL) {
        return;
    }

    for (i = 0; i < SM_PATH_CACHE_SIZE; i++) {
        if (g_sm_ctx.path_cache[i].in_use && g_sm_ctx.path_cache[i].dst_addr == dst_addr) {
            g_sm_ctx.path_cache[i].path_len = path_len;
            (void)memcpy_s(g_sm_ctx.path_cache[i].path, sizeof(g_sm_ctx.path_cache[i].path), path, path_len);
            g_sm_ctx.path_cache[i].stored_ms = (uint32_t)sm_now_ms();
            return;
        }
        if (!g_sm_ctx.path_cache[i].in_use && free_idx == SM_PATH_CACHE_SIZE) {
            free_idx = i;
        }
    }
    if (free_idx < SM_PATH_CACHE_SIZE) {
        g_sm_ctx.path_cache[free_idx].in_use = true;
        g_sm_ctx.path_cache[free_idx].dst_addr = dst_addr;
        g_sm_ctx.path_cache[free_idx].path_len = path_len;
        (void)memcpy_s(g_sm_ctx.path_cache[free_idx].path, sizeof(g_sm_ctx.path_cache[free_idx].path), path, path_len);
        g_sm_ctx.path_cache[free_idx].stored_ms = (uint32_t)sm_now_ms();
    }
}

static sm_path_cache_entry_t *sm_find_cached_path(uint8_t dst_addr)
{
    uint8_t i;
    for (i = 0; i < SM_PATH_CACHE_SIZE; i++) {
        if (g_sm_ctx.path_cache[i].in_use && g_sm_ctx.path_cache[i].dst_addr == dst_addr) {
            return &g_sm_ctx.path_cache[i];
        }
    }
    return NULL;
}

/*--------------------------------------------------------------------------
 * Relay frame via advertising / 通过广播中继帧
 *--------------------------------------------------------------------------*/

void sm_relay_frame(const uint8_t *frame, uint16_t frame_len)
{
    uint8_t next_tail;

    if (frame_len == 0 || frame_len > SM_MAX_FRAME_LEN) {
        return;
    }

    next_tail = (g_sm_ctx.relay_tail + 1) % SM_RELAY_QUEUE_SIZE;
    if (next_tail == g_sm_ctx.relay_head) {
        return; /* queue full, drop */
    }

    (void)memcpy_s(g_sm_ctx.relay_queue[g_sm_ctx.relay_tail], SM_MAX_FRAME_LEN, frame, frame_len);
    g_sm_ctx.relay_lens[g_sm_ctx.relay_tail] = frame_len;
    g_sm_ctx.relay_tail = next_tail;
}

/*--------------------------------------------------------------------------
 * Node frame handler / 节点帧处理
 *--------------------------------------------------------------------------*/

void sm_node_handle_frame(const sm_frame_view_t *frame, int8_t rssi)
{
    uint8_t relay_buf[SM_MAX_FRAME_LEN];
    uint16_t relay_len;

    if (frame == NULL) {
        return;
    }
    unused(rssi);

    switch (frame->frame_type) {
    case SM_FRAME_TYPE_RSSI_REPORT:
        break;

    case SM_FRAME_TYPE_PATH_REQ:
        if (frame->dst_addr == g_sm_ctx.local_addr) {
            osal_printk("[%u] PATH_REQ for me, src=%u seq=%u\r\n",
                g_sm_ctx.local_addr, frame->src_addr, frame->seq);
            break;
        }
        if (!sm_dedup_check(frame->frame_type, frame->src_addr, frame->seq)) {
            sm_dedup_add(frame->frame_type, frame->src_addr, frame->seq);
            relay_len = sm_build_path_req(relay_buf, sizeof(relay_buf),
                frame->src_addr, frame->dst_addr, frame->seq);
            if (relay_len > 0) {
                osal_printk("[%u] relay PATH_REQ src=%u dst=%u seq=%u\r\n",
                    g_sm_ctx.local_addr, frame->src_addr, frame->dst_addr, frame->seq);
                sm_relay_frame(relay_buf, relay_len);
            }
        }
        break;

    case SM_FRAME_TYPE_PATH_RESP:
        if (frame->src_addr == g_sm_ctx.local_addr) {
            sm_store_path(frame->dst_addr, frame->path, frame->path_len);
            osal_printk("[%u] PATH_RESP for me, dst=%u path_len=%u\r\n",
                g_sm_ctx.local_addr, frame->dst_addr, frame->path_len);
            break;
        }
        if (!sm_dedup_check(frame->frame_type, frame->src_addr, frame->seq)) {
            sm_dedup_add(frame->frame_type, frame->src_addr, frame->seq);
            relay_len = sm_build_path_resp(relay_buf, sizeof(relay_buf),
                frame->src_addr, frame->dst_addr, frame->seq,
                frame->path, frame->path_len);
            if (relay_len > 0) {
                osal_printk("[%u] relay PATH_RESP src=%u dst=%u\r\n",
                    g_sm_ctx.local_addr, frame->src_addr, frame->dst_addr);
                sm_relay_frame(relay_buf, relay_len);
            }
        }
        break;

    case SM_FRAME_TYPE_DATA:
        if (frame->dst_addr == g_sm_ctx.local_addr) {
            osal_printk("[%u] DATA for me, from=%u seq=%u len=%u\r\n",
                g_sm_ctx.local_addr, frame->src_addr, frame->seq, frame->payload_len);
            sm_node_send_ack(frame->src_addr, frame->seq);
            break;
        }
        if (frame->current_hop < frame->path_len &&
            frame->path[frame->current_hop] == g_sm_ctx.local_addr) {
            uint8_t new_hop = frame->current_hop + 1;
            relay_len = sm_build_data_frame(relay_buf, sizeof(relay_buf),
                frame->src_addr, frame->dst_addr, frame->seq,
                frame->path, frame->path_len, new_hop,
                frame->payload, frame->payload_len);
            if (relay_len > 0) {
                osal_printk("[%u] relay DATA src=%u dst=%u hop=%u/%u\r\n",
                    g_sm_ctx.local_addr, frame->src_addr, frame->dst_addr,
                    new_hop, frame->path_len);
                sm_relay_frame(relay_buf, relay_len);
            }
        }
        break;

    case SM_FRAME_TYPE_ACK:
        osal_printk("[%u] ACK from=%u seq=%u\r\n",
            g_sm_ctx.local_addr, frame->src_addr, frame->seq);
        break;

    default:
        break;
    }
}

/*--------------------------------------------------------------------------
 * Send RSSI report / 发送 RSSI 报告
 *--------------------------------------------------------------------------*/

void sm_node_send_rssi_report(void)
{
    uint8_t frame[SM_MAX_FRAME_LEN];
    uint16_t frame_len;

    frame_len = sm_build_rssi_report(frame, sizeof(frame), g_sm_ctx.local_addr);
    if (frame_len > 0) {
        sm_refresh_advertising(frame, frame_len);
    }
}

/*--------------------------------------------------------------------------
 * Send ACK / 发送确认
 *--------------------------------------------------------------------------*/

void sm_node_send_ack(uint8_t dst_addr, uint16_t seq)
{
    uint8_t frame[SM_MAX_FRAME_LEN];
    uint16_t frame_len;

    frame_len = sm_build_ack(frame, sizeof(frame), g_sm_ctx.local_addr, seq);
    if (frame_len > 0) {
        sm_relay_frame(frame, frame_len);
    }
}

/*--------------------------------------------------------------------------
 * Send PATH_REQ / 发送路径请求
 *--------------------------------------------------------------------------*/

void sm_node_send_path_req(uint8_t dst_addr)
{
    uint8_t frame[SM_MAX_FRAME_LEN];
    uint16_t frame_len;
    uint16_t seq = g_sm_ctx.next_seq++;

    frame_len = sm_build_path_req(frame, sizeof(frame), g_sm_ctx.local_addr, dst_addr, seq);
    if (frame_len > 0) {
        sm_dedup_add(SM_FRAME_TYPE_PATH_REQ, g_sm_ctx.local_addr, seq);
        sm_relay_frame(frame, frame_len);
    }
}

/*--------------------------------------------------------------------------
 * Send DATA with source route / 发送源路由数据
 *--------------------------------------------------------------------------*/

void sm_node_send_data(uint8_t dst_addr, const uint8_t *payload, uint16_t payload_len)
{
    sm_path_cache_entry_t *cached;
    uint8_t frame[SM_MAX_FRAME_LEN];
    uint16_t frame_len;
    uint16_t seq = g_sm_ctx.next_seq++;

    cached = sm_find_cached_path(dst_addr);
    if (cached == NULL) {
        sm_node_send_path_req(dst_addr);
        return;
    }

    frame_len = sm_build_data_frame(frame, sizeof(frame),
        g_sm_ctx.local_addr, dst_addr, seq,
        cached->path, cached->path_len, 1,
        payload, payload_len);
    if (frame_len > 0) {
        sm_relay_frame(frame, frame_len);
    }
}
