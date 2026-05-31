/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE mesh dongle (root) role: UART bridge, path response.
 */
#include "SM_mesh_internal.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "securec.h"
#include "app_init.h"

/*--------------------------------------------------------------------------
 * Forward frame data to PC via UART / 通过 UART 转发帧数据到 PC
 *--------------------------------------------------------------------------*/

void sm_dongle_forward_to_pc(const char *fmt, ...)
{
    char buf[SM_UART_LINE_MAX];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (len > 0) {
        sm_uart_printf("%s\r\n", buf);
    }
}

/*--------------------------------------------------------------------------
 * Store RSSI report for topology / 存储 RSSI 报告用于拓扑
 *--------------------------------------------------------------------------*/

static void sm_dongle_store_topo_report(const sm_frame_view_t *frame)
{
    if (g_sm_ctx.topo_report_count < SM_MAX_NEIGHBORS) {
        uint8_t idx = g_sm_ctx.topo_report_count;
        uint16_t total = 6 + frame->payload_len; /* 6-byte header + entries */
        if (total <= SM_MAX_FRAME_LEN && frame->payload != NULL) {
            (void)memcpy_s(g_sm_ctx.topo_reports[idx], SM_MAX_FRAME_LEN,
                frame->payload - 6, total);
            g_sm_ctx.topo_report_lens[idx] = total;
            g_sm_ctx.topo_report_count++;
        }
    }
}

/*--------------------------------------------------------------------------
 * RSSI collection: merge one report into buffer / RSSI 收集：合并一份报告
 *--------------------------------------------------------------------------*/

static void sm_dongle_collect_rssi(const sm_frame_view_t *frame)
{
    sm_rssi_collect_t *col = &g_sm_ctx.rssi_collect;
    uint32_t now = (uint32_t)sm_now_ms();

    if (frame->count == 0) {
        return;
    }

    /* Flush if switching to a different source / 切换源地址时先 flush */
    if (col->active && col->src_addr != frame->src_addr) {
        sm_dongle_flush_rssi();
    }

    /* Start new collection window if needed */
    if (!col->active) {
        col->active = true;
        col->src_addr = frame->src_addr;
        col->first_ms = now;
        col->entry_count = 0;
        /* collect start */
    }

    for (uint8_t i = 0; i < frame->count && (uint16_t)(i * 2 + 1) < frame->payload_len; i++) {
        uint8_t addr = frame->payload[i * 2];
        int8_t r = (int8_t)frame->payload[i * 2 + 1];

        /* Find or add entry */
        uint8_t j;
        for (j = 0; j < col->entry_count; j++) {
            if (col->entries[j].addr == addr) {
                col->entries[j].rssi_sum += r;
                col->entries[j].count++;
                break;
            }
        }
        if (j == col->entry_count && col->entry_count < SM_RSSI_MAX_ENTRIES) {
            col->entries[col->entry_count].addr = addr;
            col->entries[col->entry_count].rssi_sum = r;
            col->entries[col->entry_count].count = 1;
            col->entry_count++;
        }
    }
}

/*--------------------------------------------------------------------------
 * Flush collected RSSI: average and print / 刷新收集的 RSSI：求均值并打印
 *--------------------------------------------------------------------------*/

static void sm_dongle_save_to_store(uint8_t src_addr, uint8_t count, const sm_rssi_entry_t *entries)
{
    sm_rssi_store_entry_t *slot = NULL;
    uint8_t i;

    if (count == 0) {
        return;
    }

    for (i = 0; i < g_sm_ctx.rssi_store_count; i++) {
        if (g_sm_ctx.rssi_store[i].src_addr == src_addr) {
            slot = &g_sm_ctx.rssi_store[i];
            break;
        }
    }
    if (slot == NULL) {
        if (g_sm_ctx.rssi_store_count < SM_RSSI_STORE_SLOTS) {
            slot = &g_sm_ctx.rssi_store[g_sm_ctx.rssi_store_count++];
        } else {
            for (i = 0; i < SM_RSSI_STORE_SLOTS - 1; i++) {
                g_sm_ctx.rssi_store[i] = g_sm_ctx.rssi_store[i + 1];
            }
            slot = &g_sm_ctx.rssi_store[SM_RSSI_STORE_SLOTS - 1];
        }
    }

    slot->src_addr = src_addr;
    slot->count = count;
    for (i = 0; i < count; i++) {
        slot->entries[i] = entries[i];
    }
}

void sm_dongle_flush_rssi(void)
{
    sm_rssi_collect_t *col = &g_sm_ctx.rssi_collect;

    if (!col->active || col->entry_count == 0) {
        col->active = false;
        return;
    }

    sm_dongle_save_to_store(col->src_addr, col->entry_count, col->entries);

    col->active = false;
    col->entry_count = 0;
}

static void sm_dongle_store_own_rssi(void)
{
    sm_rssi_entry_t entries[SM_MAX_NEIGHBORS];
    uint8_t count = 0;
    uint8_t i;

    for (i = 0; i < SM_MAX_NEIGHBORS && count < SM_MAX_NEIGHBORS; i++) {
        if (g_sm_ctx.neighbors[i].in_use) {
            entries[count].addr = g_sm_ctx.neighbors[i].addr;
            entries[count].rssi_sum = g_sm_ctx.neighbors[i].rssi;
            entries[count].count = 1;
            count++;
        }
    }
    sm_dongle_save_to_store(g_sm_ctx.local_addr, count, entries);
}

void sm_dongle_print_rssi_store(void)
{
    uint8_t i, j;
    uint8_t raw[SM_MAX_FRAME_LEN];
    char hex[SM_UART_LINE_MAX];
    char line[SM_UART_LINE_MAX];
    int pos;

    if (g_sm_ctx.rssi_store_count == 0) {
        sm_uart_printf("NO DATA\r\n");
        return;
    }

    for (i = 0; i < g_sm_ctx.rssi_store_count; i++) {
        sm_rssi_store_entry_t *s = &g_sm_ctx.rssi_store[i];

        /* Build raw hex / 构建原始十六进制 */
        raw[0] = SM_MAGIC0; raw[1] = SM_MAGIC1; raw[2] = SM_PROTO_VERSION;
        raw[3] = SM_FRAME_TYPE_RSSI_REPORT; raw[4] = s->src_addr; raw[5] = s->count;
        pos = 0;
        for (j = 0; j < s->count; j++) {
            raw[6 + j * 2] = s->entries[j].addr;
            raw[6 + j * 2 + 1] = (uint8_t)(int8_t)(s->entries[j].rssi_sum / s->entries[j].count);
        }
        uint16_t raw_len = 6 + (uint16_t)s->count * 2;
        pos = 0;
        for (j = 0; j < raw_len && pos < (int)(sizeof(hex) - 3); j++) {
            pos += snprintf_s(hex + pos, sizeof(hex) - (uint16_t)pos,
                sizeof(hex) - (uint16_t)pos - 1, "%02X", raw[j]);
        }
        sm_uart_printf("HEX %s\r\n", hex);

        /* Human-readable / 可读格式 */
        pos = snprintf_s(line, sizeof(line), sizeof(line) - 1,
            "RSSI_REPORT src=%u count=%u", s->src_addr, s->count);
        for (j = 0; j < s->count && pos < (int)(sizeof(line) - 16); j++) {
            int8_t avg = (int8_t)(s->entries[j].rssi_sum / s->entries[j].count);
            pos += snprintf_s(line + pos, sizeof(line) - (uint16_t)pos,
                sizeof(line) - (uint16_t)pos - 1, " [%u:%d]", s->entries[j].addr, avg);
        }
        sm_uart_printf("%s\r\n", line);
    }
}

/*--------------------------------------------------------------------------
 * Dongle frame handler / dongle 帧处理
 *--------------------------------------------------------------------------*/

void sm_dongle_handle_frame(const sm_frame_view_t *frame, int8_t rssi)
{
    uint8_t relay_buf[SM_MAX_FRAME_LEN];
    uint16_t relay_len;

    if (frame == NULL) {
        return;
    }
    unused(rssi);

    switch (frame->frame_type) {
    case SM_FRAME_TYPE_RSSI_REPORT:
        sm_dongle_store_topo_report(frame);
        sm_dongle_collect_rssi(frame);
        break;

    case SM_FRAME_TYPE_PATH_REQ:
        /* Forward PATH_REQ to PC / 转发 PATH_REQ 到 PC */
        osal_printk("[0] PATH_REQ from=%u dst=%u seq=%u\r\n",
            frame->src_addr, frame->dst_addr, frame->seq);
        sm_uart_printf("PATH_REQ %u %u %u\r\n", frame->src_addr, frame->dst_addr, frame->seq);
        break;

    case SM_FRAME_TYPE_PATH_RESP:
        /* PATH_RESP received via broadcast, relay it / 通过广播收到 PATH_RESP，中继 */
        if (frame->src_addr == g_sm_ctx.local_addr) {
            break;
        }
        if (!sm_dedup_check(frame->frame_type, frame->src_addr, frame->seq)) {
            sm_dedup_add(frame->frame_type, frame->src_addr, frame->seq);
            relay_len = sm_build_path_resp(relay_buf, sizeof(relay_buf),
                frame->src_addr, frame->dst_addr, frame->seq,
                frame->path, frame->path_len);
            if (relay_len > 0) {
                sm_relay_frame(relay_buf, relay_len);
            }
        }
        break;

    case SM_FRAME_TYPE_DATA:
        if (frame->dst_addr == g_sm_ctx.local_addr) {
            /* Data for dongle, forward to PC / 数据发给 dongle，转发到 PC */
            char hex[SM_UART_LINE_MAX];
            uint16_t i;
            int pos = 0;
            for (i = 0; i < frame->payload_len && pos < (int)(sizeof(hex) - 3); i++) {
                pos += snprintf_s(hex + pos, sizeof(hex) - (uint16_t)pos, sizeof(hex) - (uint16_t)pos - 1,
                    "%02X", frame->payload[i]);
            }
            sm_uart_printf("DATA %u %u %u %s\r\n", frame->src_addr, frame->dst_addr, frame->seq, hex);
            break;
        }
        /* Forward DATA along path / 沿路径转发 DATA */
        if (frame->current_hop < frame->path_len &&
            frame->path[frame->current_hop] == g_sm_ctx.local_addr) {
            uint8_t new_hop = frame->current_hop + 1;
            relay_len = sm_build_data_frame(relay_buf, sizeof(relay_buf),
                frame->src_addr, frame->dst_addr, frame->seq,
                frame->path, frame->path_len, new_hop,
                frame->payload, frame->payload_len);
            if (relay_len > 0) {
                osal_printk("[0] relay DATA src=%u dst=%u hop=%u/%u\r\n",
                    frame->src_addr, frame->dst_addr, new_hop, frame->path_len);
                sm_relay_frame(relay_buf, relay_len);
            }
        }
        break;

    case SM_FRAME_TYPE_ACK:
        /* Forward ACK to PC / 转发 ACK 到 PC */
        sm_uart_printf("ACK %u %u\r\n", frame->src_addr, frame->seq);
        break;

    default:
        break;
    }
}

/*--------------------------------------------------------------------------
 * Send PATH_RESP from PC command / 从 PC 命令发送 PATH_RESP
 *--------------------------------------------------------------------------*/

void sm_dongle_send_path_resp(uint8_t src_addr, uint8_t dst_addr,
    const uint8_t *path, uint8_t path_len)
{
    uint8_t frame[SM_MAX_FRAME_LEN];
    uint16_t frame_len;
    uint16_t seq = g_sm_ctx.next_seq++;

    frame_len = sm_build_path_resp(frame, sizeof(frame), src_addr, dst_addr, seq, path, path_len);
    if (frame_len > 0) {
        sm_relay_frame(frame, frame_len);
    }
}

/*--------------------------------------------------------------------------
 * Periodic dongle tick / 周期性 dongle 处理
 *--------------------------------------------------------------------------*/

void sm_dongle_role_tick(void)
{
    sm_rssi_collect_t *col = &g_sm_ctx.rssi_collect;
    static uint32_t last_own_rssi_ms = 0;
    uint32_t now = (uint32_t)sm_now_ms();

    if (col->active) {
        if ((now - col->first_ms) >= SM_RSSI_COLLECT_WINDOW_MS) {
            sm_dongle_flush_rssi();
        }
    }

    /* Periodically store dongle's own neighbor RSSI / 周期性存储 dongle 自身邻居 RSSI */
    if ((now - last_own_rssi_ms) >= SM_RSSI_REPORT_PERIOD_MS) {
        last_own_rssi_ms = now;
        sm_dongle_store_own_rssi();
    }
}

/*--------------------------------------------------------------------------
 * Dongle app_run entry / dongle 应用入口
 *--------------------------------------------------------------------------*/

#if defined(CONFIG_SAMPLE_SUPPORT_SLE_MESH_DONGLE_SAMPLE)
static void sle_mesh_dongle_entry(void)
{
    (void)sm_mesh_dongle_init();
}

app_run(sle_mesh_dongle_entry);
#endif
