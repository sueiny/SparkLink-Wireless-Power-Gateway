/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test UART helpers.
 */
#include "ST_test_internal.h"

#include "at_product.h"
#include "osal_interrupt.h"
#include "uart.h"

/* -------------------------------------------------------------------------- */
/* UART command parsing helpers / UART 命令解析辅助                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Parse the destination node id from the UART command prefix.
 * @brief 从 UART 命令前半段解析目标节点 ID，格式为十进制数字。
 */
static bool sle_tree_parse_decimal(const char *text, uint16_t *value)
{
    uint32_t result = 0;

    if (text == NULL || value == NULL || *text == '\0') {
        return false;
    }
    while (*text >= '0' && *text <= '9') {
        result = result * 10U + (uint32_t)(*text - '0');
        if (result > 0xFFFFU) {
            return false;
        }
        text++;
    }
    if (*text != '\0' && *text != ' ' && *text != '\t') {
        return false;
    }
    *value = (uint16_t)result;
    return true;
}

/**
 * @brief Parse a HEX payload string from the UART command tail.
 * @brief 从 UART 命令后半段解析十六进制负载文本，生成实际二进制 payload。
 */
static bool sle_tree_parse_hex_payload(const char *text, uint8_t *payload, uint16_t *payload_len)
{
    uint16_t length = 0;

    if (text == NULL || payload == NULL || payload_len == NULL) {
        return false;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    while (*text != '\0') {
        int32_t high;
        int32_t low;

        if (*text == ' ' || *text == '\t') {
            text++;
            continue;
        }
        high = sle_tree_hex_value(*text++);
        if (high < 0) {
            return false;
        }
        if (*text == '\0') {
            return false;
        }
        low = sle_tree_hex_value(*text++);
        if (low < 0) {
            return false;
        }
        if (length >= SLE_TREE_MAX_PAYLOAD_LEN) {
            return false;
        }
        payload[length++] = (uint8_t)((high << 4) | low);
    }
    *payload_len = length;
    return (length > 0);
}

/**
 * @brief Parse and execute one complete UART command line.
 * @brief 解析并执行一整行 UART 命令，当前格式为 `<dst_node_id> <HEX_PAYLOAD>`。
 */
static void sle_tree_process_uart_command(const char *line)
{
    const char *payload_text;
    const char *cursor = line;
    uint16_t dst_node_id = 0;
    uint16_t payload_len = 0;
    uint8_t payload[SLE_TREE_MAX_PAYLOAD_LEN] = {0};

    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    payload_text = cursor;
    while (*payload_text != '\0' && *payload_text != ' ' && *payload_text != '\t') {
        payload_text++;
    }
    if (!sle_tree_parse_decimal(cursor, &dst_node_id)) {
        sle_tree_uart_printf("USAGE: <dst_node_id> <HEX_PAYLOAD>\r\n");
        return;
    }
    if (!sle_tree_parse_hex_payload(payload_text, payload, &payload_len)) {
        sle_tree_uart_printf("USAGE: <dst_node_id> <HEX_PAYLOAD>\r\n");
        return;
    }
    sle_tree_send_local_data(dst_node_id, payload, payload_len);
}

/**
 * @brief Forward one AT command line to the system AT channel on bus0.
 * @brief 把一整行 AT 命令转发给系统 AT 通道，避免被 sample 命令解析抢占。
 */
static void sle_tree_forward_at_command(const char *line)
{
    char at_line[SLE_TREE_UART_LINE_MAX + 3];
    int ret;

    if (line == NULL) {
        return;
    }
    ret = snprintf_s(at_line, sizeof(at_line), sizeof(at_line) - 1U, "%s\r\n", line);
    if (ret <= 0) {
        return;
    }
    (void)uapi_at_channel_data_recv(AT_UART_PORT, (uint8_t *)at_line, (uint32_t)ret);
}

/**
 * @brief Dispatch one complete UART line either to AT or to sample command parsing.
 * @brief 对一整行 UART 文本做分流：AT 命令转给系统，其余交给 sample 命令解析。
 */
static void sle_tree_dispatch_uart_line(const char *line)
{
    const char *cursor = line;

    if (cursor == NULL) {
        return;
    }
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if ((cursor[0] == 'A' || cursor[0] == 'a') && (cursor[1] == 'T' || cursor[1] == 't') &&
        (cursor[2] == '\0' || cursor[2] == '+' || cursor[2] == '?' || cursor[2] == '\r' || cursor[2] == ' ')) {
        sle_tree_forward_at_command(cursor);
        return;
    }
    if ((cursor[0] == 'L' || cursor[0] == 'l') && (cursor[1] == 'O' || cursor[1] == 'o') &&
        (cursor[2] == 'S' || cursor[2] == 's') && (cursor[3] == 'S' || cursor[3] == 's') &&
        (cursor[4] == '\0' || cursor[4] == ' ' || cursor[4] == '\t')) {
        sle_tree_loss_reset();
        return;
    }
    if ((cursor[0] == 't' || cursor[0] == 'T') && (cursor[1] == 'o' || cursor[1] == 'O') &&
        (cursor[2] == 'p' || cursor[2] == 'P') && (cursor[3] == 'u' || cursor[3] == 'U') &&
        (cursor[4] == '?') && (cursor[5] == '\0' || cursor[5] == ' ' || cursor[5] == '\t')) {
        sle_tree_root_print_topology_tree();
        return;
    }
    sle_tree_process_uart_command(cursor);
}

/* -------------------------------------------------------------------------- */
/* UART receive callback and ring buffer / UART 接收回调与环形缓冲区            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Push one byte into UART RX ring buffer from interrupt callback.
 * @brief 在中断回调里把一个字节写入 UART RX 环形缓冲区。
 */
static void sle_tree_uart_ring_push_byte(uint8_t byte)
{
    uint16_t next_pos = (uint16_t)((g_sle_tree_ctx.uart_rx_write_pos + 1U) % SLE_TREE_UART_RX_RING_SIZE);

    if (next_pos == g_sle_tree_ctx.uart_rx_read_pos) {
        g_sle_tree_ctx.uart_rx_overflow = true;
        return;
    }
    g_sle_tree_ctx.uart_rx_ring[g_sle_tree_ctx.uart_rx_write_pos] = byte;
    g_sle_tree_ctx.uart_rx_write_pos = next_pos;
}

/**
 * @brief UART RX interrupt callback: only move bytes into ring buffer.
 * @brief UART RX 中断回调：只负责把字节搬进环形缓冲区。
 */
static void sle_tree_uart_rx_callback(const void *buffer, uint16_t length, bool error)
{
    const uint8_t *data = (const uint8_t *)buffer;
    uint16_t i;

    if (error || data == NULL || length == 0) {
        return;
    }
    for (i = 0; i < length; i++) {
        sle_tree_uart_ring_push_byte(data[i]);
    }
}

/**
 * @brief Register bus0 RX callback so UART input is interrupt-driven.
 * @brief 给 bus0 注册 RX 回调，让 UART 输入改为中断驱动。
 */
void sle_tree_uart_register_rx_callback(void)
{
    uapi_uart_unregister_rx_callback(SLE_TREE_UART_BUS_ID);
    (void)uapi_uart_register_rx_callback(SLE_TREE_UART_BUS_ID,
        UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 1, sle_tree_uart_rx_callback);
}

/**
 * @brief Pop one byte from UART RX ring buffer in worker context.
 * @brief 在线程上下文中从 UART RX 环形缓冲区弹出一个字节。
 */
static bool sle_tree_uart_ring_pop_byte(uint8_t *byte)
{
    uint32_t irq_sts;

    if (byte == NULL) {
        return false;
    }
    irq_sts = osal_irq_lock();
    if (g_sle_tree_ctx.uart_rx_read_pos == g_sle_tree_ctx.uart_rx_write_pos) {
        osal_irq_restore(irq_sts);
        return false;
    }
    *byte = g_sle_tree_ctx.uart_rx_ring[g_sle_tree_ctx.uart_rx_read_pos];
    g_sle_tree_ctx.uart_rx_read_pos =
        (uint16_t)((g_sle_tree_ctx.uart_rx_read_pos + 1U) % SLE_TREE_UART_RX_RING_SIZE);
    osal_irq_restore(irq_sts);
    return true;
}

/* -------------------------------------------------------------------------- */
/* UART receive worker / UART 接收线程侧处理                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Drain UART RX ring buffer, assemble line-based commands, and dispatch on line end.
 * @brief 从 UART RX 环形缓冲区取字节，按行拼接命令，并在回车换行时触发解析执行。
 */
void sle_tree_handle_uart_input(void)
{
    uint8_t rx_byte;

    while (sle_tree_uart_ring_pop_byte(&rx_byte)) {
        char ch = (char)rx_byte;

        if (ch == '\r' || ch == '\n') {
            if (g_sle_tree_ctx.uart_line_len > 0) {
                g_sle_tree_ctx.uart_line[g_sle_tree_ctx.uart_line_len] = '\0';
                sle_tree_dispatch_uart_line(g_sle_tree_ctx.uart_line);
                g_sle_tree_ctx.uart_line_len = 0;
                g_sle_tree_ctx.uart_line[0] = '\0';
            }
            continue;
        }
        if (g_sle_tree_ctx.uart_line_len + 1U < sizeof(g_sle_tree_ctx.uart_line)) {
            g_sle_tree_ctx.uart_line[g_sle_tree_ctx.uart_line_len++] = ch;
            g_sle_tree_ctx.uart_line[g_sle_tree_ctx.uart_line_len] = '\0';
        } else {
            g_sle_tree_ctx.uart_line_len = 0;
            g_sle_tree_ctx.uart_line[0] = '\0';
        }
    }
    if (g_sle_tree_ctx.uart_rx_overflow) {
        g_sle_tree_ctx.uart_rx_overflow = false;
        sle_tree_uart_printf("UART RX overflow\r\n");
    }
}
