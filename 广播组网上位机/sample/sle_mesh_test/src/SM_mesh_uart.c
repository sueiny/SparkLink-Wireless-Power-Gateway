/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE mesh UART interface for dongle <-> PC communication.
 */
#include "SM_mesh_internal.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "securec.h"
#include "soc_osal.h"
#include "osal_interrupt.h"
#include "uart.h"

/*--------------------------------------------------------------------------
 * Ring buffer helpers / 环形缓冲辅助
 *--------------------------------------------------------------------------*/

static void sm_uart_ring_push(uint8_t byte)
{
    uint16_t next = (g_sm_ctx.uart_rx_write_pos + 1) % SM_UART_RX_RING_SIZE;
    if (next == g_sm_ctx.uart_rx_read_pos) {
        return; /* full */
    }
    g_sm_ctx.uart_rx_ring[g_sm_ctx.uart_rx_write_pos] = byte;
    g_sm_ctx.uart_rx_write_pos = next;
}

static bool sm_uart_ring_pop(uint8_t *byte)
{
    uint32_t irq_sts;

    if (byte == NULL) {
        return false;
    }
    irq_sts = osal_irq_lock();
    if (g_sm_ctx.uart_rx_read_pos == g_sm_ctx.uart_rx_write_pos) {
        osal_irq_restore(irq_sts);
        return false; /* empty */
    }
    *byte = g_sm_ctx.uart_rx_ring[g_sm_ctx.uart_rx_read_pos];
    g_sm_ctx.uart_rx_read_pos = (uint16_t)((g_sm_ctx.uart_rx_read_pos + 1U) % SM_UART_RX_RING_SIZE);
    osal_irq_restore(irq_sts);
    return true;
}

/*--------------------------------------------------------------------------
 * UART RX callback (ISR context) / UART 接收回调（中断上下文）
 *--------------------------------------------------------------------------*/

static void sm_uart_rx_callback(const void *buffer, uint16_t length, bool error)
{
    const uint8_t *data = (const uint8_t *)buffer;
    uint16_t i;

    if (error || data == NULL || length == 0) {
        return;
    }
    for (i = 0; i < length; i++) {
        sm_uart_ring_push(data[i]);
    }
}

/*--------------------------------------------------------------------------
 * Register UART RX callback / 注册 UART 接收回调
 *--------------------------------------------------------------------------*/

void sm_uart_register_rx(void)
{
    uapi_uart_unregister_rx_callback(SM_UART_BUS_ID);
    (void)uapi_uart_register_rx_callback(SM_UART_BUS_ID,
        UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 1, sm_uart_rx_callback);
}

/*--------------------------------------------------------------------------
 * Parse decimal number from string / 从字符串解析十进制数
 *--------------------------------------------------------------------------*/

static bool sm_parse_uint8(const char *str, uint8_t *out)
{
    uint16_t val = 0;
    const char *p = str;
    if (*p < '0' || *p > '9') {
        return false;
    }
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (uint16_t)(*p - '0');
        if (val > 255) return false;
        p++;
    }
    *out = (uint8_t)val;
    return true;
}

/*--------------------------------------------------------------------------
 * Parse hex payload from string / 从字符串解析十六进制载荷
 *--------------------------------------------------------------------------*/

static uint16_t sm_parse_hex_payload(const char *hex, uint8_t *out, uint16_t max_len)
{
    uint16_t len = 0;
    uint8_t hi, lo;

    while (*hex != '\0' && *(hex + 1) != '\0' && len < max_len) {
        hi = (uint8_t)hex[0];
        lo = (uint8_t)hex[1];
        uint8_t byte_val = 0;

        if (hi >= '0' && hi <= '9') byte_val = (uint8_t)((hi - '0') << 4);
        else if (hi >= 'A' && hi <= 'F') byte_val = (uint8_t)((hi - 'A' + 10) << 4);
        else if (hi >= 'a' && hi <= 'f') byte_val = (uint8_t)((hi - 'a' + 10) << 4);
        else break;

        if (lo >= '0' && lo <= '9') byte_val |= (uint8_t)(lo - '0');
        else if (lo >= 'A' && lo <= 'F') byte_val |= (uint8_t)(lo - 'A' + 10);
        else if (lo >= 'a' && lo <= 'f') byte_val |= (uint8_t)(lo - 'a' + 10);
        else break;

        out[len++] = byte_val;
        hex += 2;
    }
    return len;
}

/*--------------------------------------------------------------------------
 * Dispatch UART command line / 分发 UART 命令行
 *--------------------------------------------------------------------------*/

static void sm_dispatch_uart_line(const char *line)
{
    const char *p;

    if (line == NULL || line[0] == '\0') {
        return;
    }

    /* PATH_RESP <src> <dst> <path_len> <addr0> <addr1> ... */
    if (strncmp(line, "PATH_RESP ", 10) == 0) {
        uint8_t src, dst, path_len;
        uint8_t path[SLE_ADDR_LEN];

        p = line + 10;
        if (!sm_parse_uint8(p, &src)) return;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (!sm_parse_uint8(p, &dst)) return;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (!sm_parse_uint8(p, &path_len)) return;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;

        for (uint8_t i = 0; i < path_len && i < SLE_ADDR_LEN; i++) {
            if (!sm_parse_uint8(p, &path[i])) return;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
        sm_dongle_send_path_resp(src, dst, path, path_len);
        return;
    }

    /* SEND <dst> <path_len> <addr0> <addr1> ... <hex_payload> */
    if (strncmp(line, "SEND ", 5) == 0) {
        uint8_t dst, path_len;
        uint8_t path[SLE_ADDR_LEN];
        uint8_t payload[SM_UART_LINE_MAX];

        p = line + 5;
        if (!sm_parse_uint8(p, &dst)) return;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (!sm_parse_uint8(p, &path_len)) return;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        for (uint8_t i = 0; i < path_len && i < SLE_ADDR_LEN; i++) {
            if (!sm_parse_uint8(p, &path[i])) return;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
        }
        uint16_t payload_len = sm_parse_hex_payload(p, payload, sizeof(payload));
        if (payload_len > 0 && path_len > 0) {
            uint8_t frame[SM_MAX_FRAME_LEN];
            uint16_t seq = g_sm_ctx.next_seq++;
            uint16_t frame_len = sm_build_data_frame(frame, sizeof(frame),
                g_sm_ctx.local_addr, dst, seq, path, path_len, 1, payload, payload_len);
            if (frame_len > 0) {
                sm_relay_frame(frame, frame_len);
            }
        }
        return;
    }

    /* TOPO? */
    if (strncmp(line, "TOPO?", 5) == 0) {
        osal_printk("TOPO report_count=%u\r\n", g_sm_ctx.topo_report_count);
        return;
    }

    /* RSSI_REQ */
    if (strncmp(line, "RSSI_REQ", 8) == 0) {
        sm_dongle_print_rssi_store();
        return;
    }

    osal_printk("ERR unknown cmd\r\n");
}

/*--------------------------------------------------------------------------
 * Process UART input from ring buffer / 从环形缓冲处理 UART 输入
 *--------------------------------------------------------------------------*/

void sm_uart_handle_input(void)
{
    uint8_t byte;

    while (sm_uart_ring_pop(&byte)) {
        if (byte == '\r' || byte == '\n') {
            if (g_sm_ctx.uart_line_len > 0) {
                g_sm_ctx.uart_line[g_sm_ctx.uart_line_len] = '\0';
                sm_dispatch_uart_line(g_sm_ctx.uart_line);
                g_sm_ctx.uart_line_len = 0;
            }
        } else {
            if (g_sm_ctx.uart_line_len < SM_UART_LINE_MAX - 1) {
                g_sm_ctx.uart_line[g_sm_ctx.uart_line_len++] = (char)byte;
            }
        }
    }
}

/*--------------------------------------------------------------------------
 * UART printf helper / UART 打印辅助
 *--------------------------------------------------------------------------*/

void sm_uart_printf(const char *fmt, ...)
{
    char buf[SM_UART_LINE_MAX];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (len > 0) {
        osal_printk("%s", buf);
    }
}
