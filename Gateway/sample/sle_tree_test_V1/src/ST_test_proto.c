/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test frame and transfer helpers.
 */
#include "ST_test_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "securec.h"
#include "sle_errcode.h"
#include "sle_ssap_client.h"
#include "sle_ssap_server.h"
#include "soc_osal.h"
#include "uart.h"

/* -------------------------------------------------------------------------- */
/* Basic byte-order helpers / 基础字节序辅助                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Read a little-endian 16-bit value from raw bytes.
 * @brief 从原始字节流中读取一个小端格式的 16 位整数。
 */
uint16_t sle_tree_get_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

/**
 * @brief Write a 16-bit value into raw bytes using little-endian layout.
 * @brief 以小端格式把 16 位整数写回到原始字节数组中。
 */
void sle_tree_put_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

/**
 * @brief Print one formatted log line.
 * @brief 输出一行格式化日志。
 */
void sle_tree_uart_printf(const char *fmt, ...)
{
    va_list args;
    char buffer[256] = {0};
    int ret;

    va_start(args, fmt);
    ret = vsnprintf_s(buffer, sizeof(buffer), sizeof(buffer) - 1, fmt, args);
    va_end(args);
    if (ret <= 0) {
        return;
    }
    osal_printk("%s", buffer);
#if SLE_TREE_UART_DIRECT_ECHO
    (void)uapi_uart_write(SLE_TREE_UART_BUS_ID, (const uint8_t *)buffer, (uint32_t)strlen(buffer), 0);
#endif
}

/* -------------------------------------------------------------------------- */
/* Tree frame encode / decode helpers / 树状网络帧编解码辅助                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Build one internal tree frame used on the SLE data path.
 * @brief 按样例内部协议拼装一帧树状网络报文，供 SLE 链路上下行传输使用。
 */
uint16_t sle_tree_build_frame(uint8_t frame_type, uint16_t dst_node_id, const uint8_t *payload,
    uint16_t payload_len, uint8_t *buffer, uint16_t buffer_len)
{
    uint16_t total_len = (uint16_t)(SLE_TREE_FRAME_HEADER_LEN + payload_len);

    if (buffer == NULL || buffer_len < total_len || payload_len > SLE_TREE_MAX_PAYLOAD_LEN) {
        return 0;
    }
    buffer[0] = SLE_TREE_MAGIC0;
    buffer[1] = SLE_TREE_MAGIC1;
    buffer[2] = SLE_TREE_PROTO_VERSION;
    buffer[3] = frame_type;
    buffer[4] = g_sle_tree_ctx.role;
    sle_tree_put_le16(&buffer[5], g_sle_tree_ctx.cfg.node_id);
    sle_tree_put_le16(&buffer[7], dst_node_id);
    if (frame_type == SLE_TREE_FRAME_TYPE_DATA) {
        sle_tree_put_le16(&buffer[9], ++g_sle_tree_ctx.next_data_seq);
    } else {
        sle_tree_put_le16(&buffer[9], ++g_sle_tree_ctx.next_seq);
    }
    sle_tree_put_le16(&buffer[11], payload_len);
    if (payload_len > 0 && payload != NULL) {
        (void)memcpy_s(&buffer[SLE_TREE_FRAME_HEADER_LEN], buffer_len - SLE_TREE_FRAME_HEADER_LEN, payload, payload_len);
    }
    return total_len;
}

/**
 * @brief Parse one internal tree frame from a raw receive buffer.
 * @brief 从原始接收缓冲区中解析一帧树状网络内部报文。
 */
bool sle_tree_parse_frame(const uint8_t *buffer, uint16_t buffer_len, sle_tree_frame_view_t *frame)
{
    uint16_t payload_len;

    if (buffer == NULL || frame == NULL || buffer_len < SLE_TREE_FRAME_HEADER_LEN) {
        return false;
    }
    if (buffer[0] != SLE_TREE_MAGIC0 || buffer[1] != SLE_TREE_MAGIC1 || buffer[2] != SLE_TREE_PROTO_VERSION) {
        return false;
    }
    payload_len = sle_tree_get_le16(&buffer[11]);
    if ((uint16_t)(SLE_TREE_FRAME_HEADER_LEN + payload_len) > buffer_len) {
        return false;
    }
    frame->frame_type = buffer[3];
    frame->src_role = buffer[4];
    frame->src_node_id = sle_tree_get_le16(&buffer[5]);
    frame->dst_node_id = sle_tree_get_le16(&buffer[7]);
    frame->seq = sle_tree_get_le16(&buffer[9]);
    frame->payload_len = payload_len;
    frame->payload = &buffer[SLE_TREE_FRAME_HEADER_LEN];
    return true;
}

/**
 * @brief Convert a binary payload to upper-case hex text for UART logs.
 * @brief 把二进制负载转成大写十六进制文本，方便串口日志直接阅读。
 */
static void sle_tree_hex_encode(const uint8_t *data, uint16_t data_len, char *out, uint16_t out_len)
{
    static const char hex_table[] = "0123456789ABCDEF";
    uint16_t i;

    if (out == NULL || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (data == NULL) {
        return;
    }
    for (i = 0; i < data_len && (uint32_t)(i * 2 + 2) < out_len; i++) {
        out[i * 2] = hex_table[(data[i] >> 4U) & 0x0FU];
        out[i * 2 + 1] = hex_table[data[i] & 0x0FU];
        out[i * 2 + 2] = '\0';
    }
}

/**
 * @brief Convert a single hex character to its numeric value.
 * @brief 把单个十六进制字符转换成对应数值，非法字符返回 -1。
 */
int32_t sle_tree_hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

/* -------------------------------------------------------------------------- */
/* UART report helpers / UART 日志上报辅助                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Print one received data payload in the unified UART log format.
 * @brief 按统一串口日志格式打印一条收到的数据负载。
 */
void sle_tree_report_data(uint16_t src_node_id, const uint8_t *data, uint16_t data_len)
{
    static const uint8_t auto_up_payload[] = "AUTO-UP";
    char hex[(SLE_TREE_MAX_PAYLOAD_LEN * 2) + 1] = {0};

    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT && data != NULL &&
        data_len == (sizeof(auto_up_payload) - 1U) &&
        memcmp(data, auto_up_payload, sizeof(auto_up_payload) - 1U) == 0) {
        return;
    }
    sle_tree_hex_encode(data, data_len, hex, sizeof(hex));
    sle_tree_uart_printf("SRC=%u LEN=%u DATA=%s\r\n", src_node_id, data_len, hex);
}

/**
 * @brief Print one heartbeat indication and the direct connection it arrived on.
 * @brief 打印一条心跳日志，并附带指出该心跳是从哪个直连连接到达的。
 */
void sle_tree_report_heartbeat(uint16_t src_node_id, uint16_t via_conn_id)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        return;
    }
    sle_tree_uart_printf("HB SRC=%u VIA=%u\r\n", src_node_id, via_conn_id);
}

/**
 * @brief Print a unified unreachable report for manual or automatic sends.
 * @brief 对手工发包或自动发包统一打印目标不可达日志。
 */
void sle_tree_report_unreachable(uint16_t dst_node_id)
{
    sle_tree_uart_printf("DST=%u ERR=unreachable\r\n", dst_node_id);
}

/* -------------------------------------------------------------------------- */
/* Low-level send helpers / 底层发送辅助                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Send one frame to a child node through server-side notify.
 * @brief 通过 server 侧 notify 把一帧数据发给某个下游子节点。
 */
errcode_t sle_tree_send_notify(uint16_t conn_id, const uint8_t *data, uint16_t data_len)
{
    ssaps_ntf_ind_t notify = {0};

    if (!sle_tree_role_has_server() || !g_sle_tree_ctx.server_ready || conn_id == SLE_TREE_INVALID_CONN_ID) {
        return ERRCODE_FAIL;
    }
    notify.handle = g_sle_tree_ctx.property_handle;
    notify.type = SSAP_PROPERTY_TYPE_VALUE;
    notify.value_len = data_len;
    notify.value = (uint8_t *)data;
    return ssaps_notify_indicate(g_sle_tree_ctx.server_id, conn_id, &notify);
}

/**
 * @brief Send one frame to the current parent through client-side write.
 * @brief 通过 client 侧 write 把一帧数据发给当前父节点。
 */
errcode_t sle_tree_send_uplink_write(const uint8_t *data, uint16_t data_len)
{
    ssapc_write_param_t write_param = {0};
    errcode_t ret;

    if (!sle_tree_role_has_client() || !g_sle_tree_ctx.uplink.connected || !g_sle_tree_ctx.uplink.handle_ready) {
        return ERRCODE_FAIL;
    }
    write_param.handle = g_sle_tree_ctx.uplink.write_handle;
    write_param.type = SSAP_PROPERTY_TYPE_VALUE;
    write_param.data_len = data_len;
    write_param.data = (uint8_t *)data;
    ret = ssapc_write_req(SLE_TREE_CLIENT_ID, g_sle_tree_ctx.uplink.conn_id, &write_param);
    if (ret != ERRCODE_SUCC) {
        if (ret == ERRCODE_SLE_BUSY) {
            /* 协议栈写队列满（最多5个），不是断连，返回失败让调用方入队重试 */
            return ERRCODE_FAIL;
        }
        sle_tree_uart_printf("%s uplink write failed conn=%u ret=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.uplink.conn_id, ret);
        sle_tree_handle_uplink_disconnected();
    }
    return ret;
}

/* -------------------------------------------------------------------------- */
/* Local injection and self-handling / 本地注入与本机处理                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Inject one payload from local UART/application context into the tree.
 * @brief 把本地 UART 或业务侧输入的数据封装成树内报文并注入当前网络。
 */
void sle_tree_send_local_data(uint16_t dst_node_id, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[SLE_TREE_MAX_FRAME_LEN] = {0};
    uint16_t frame_len;
    sle_tree_route_t *route;

    frame_len = sle_tree_build_frame(SLE_TREE_FRAME_TYPE_DATA, dst_node_id, payload, payload_len, frame, sizeof(frame));
    if (frame_len == 0) {
        return;
    }
    if (dst_node_id == g_sle_tree_ctx.cfg.node_id) {
        sle_tree_report_data(g_sle_tree_ctx.cfg.node_id, payload, payload_len);
        return;
    }
    route = sle_tree_find_route(dst_node_id);
    if (route != NULL && sle_tree_role_has_server()) {
        if (sle_tree_send_notify(route->next_hop_conn_id, frame, frame_len) == ERRCODE_SUCC) {
            return;
        }
    }
    if (sle_tree_role_has_client() && sle_tree_send_uplink_write(frame, frame_len) == ERRCODE_SUCC) {
        return;
    }
    sle_tree_uart_printf("%s local send fail dst=%u route=%s uplink_connected=%u handle_ready=%u\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, dst_node_id, (route == NULL) ? "none" : "notify_fail",
        g_sle_tree_ctx.uplink.connected ? 1 : 0, g_sle_tree_ctx.uplink.handle_ready ? 1 : 0);
    sle_tree_report_unreachable(dst_node_id);
}

/**
 * @brief Send one heartbeat frame to the current parent if uplink is ready.
 * @brief 当上行父连接已就绪时，向父节点发送一帧心跳报文。
 */
void sle_tree_send_heartbeat(void)
{
    uint8_t frame[SLE_TREE_FRAME_HEADER_LEN] = {0};
    uint16_t frame_len;

    if (!sle_tree_role_has_client() || !g_sle_tree_ctx.uplink.connected || !g_sle_tree_ctx.uplink.handle_ready) {
        return;
    }
    frame_len = sle_tree_build_frame(SLE_TREE_FRAME_TYPE_HEARTBEAT, SLE_TREE_ANY_NODE_ID, NULL, 0, frame, sizeof(frame));
    if (frame_len > 0) {
        (void)sle_tree_send_uplink_write(frame, frame_len);
    }
}

/**
 * @brief Send depth update notification to all direct children after relay reconnection.
 * @brief relay 重连后深度变化时，向所有直连子节点发送深度更新通知。
 */
void sle_tree_send_depth_update_to_children(uint8_t new_depth)
{
    uint8_t payload[1];
    uint8_t frame[SLE_TREE_FRAME_HEADER_LEN + 1] = {0};
    uint16_t frame_len;
    uint8_t i;

    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_RELAY) {
        return;
    }
    payload[0] = new_depth;
    frame_len = sle_tree_build_frame(SLE_TREE_FRAME_TYPE_DEPTH_UPDATE, SLE_TREE_ANY_NODE_ID,
        payload, sizeof(payload), frame, sizeof(frame));
    if (frame_len == 0) {
        return;
    }
    for (i = 0; i < SLE_TREE_MAX_CHILDREN; i++) {
        if (g_sle_tree_ctx.children[i].in_use) {
            (void)sle_tree_send_notify(g_sle_tree_ctx.children[i].conn_id, frame, frame_len);
        }
    }
}

/**
 * @brief Send one compact topology summary from relay to root.
 * @brief relay 向 root 发送一帧紧凑拓扑摘要，仅包含自己和直连孩子关系。
 */
void sle_tree_send_topo_summary(void)
{
    uint8_t payload[3 + (SLE_TREE_MAX_CHILDREN * 3)] = {0};
    uint8_t frame[SLE_TREE_MAX_FRAME_LEN] = {0};
    uint16_t frame_len;
    uint16_t payload_len = 3;
    uint8_t child_count = 0;
    uint8_t i;

    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_RELAY || !g_sle_tree_ctx.uplink.connected ||
        !g_sle_tree_ctx.uplink.handle_ready || g_sle_tree_ctx.uplink.root_node_id == SLE_TREE_INVALID_NODE_ID) {
        sle_tree_uart_printf("%s topo summary BLOCKED role=%u conn=%u ready=%u root=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.role, g_sle_tree_ctx.uplink.connected,
            g_sle_tree_ctx.uplink.handle_ready, g_sle_tree_ctx.uplink.root_node_id);
        return;
    }

    sle_tree_put_le16(&payload[0], g_sle_tree_ctx.cfg.node_id);
    for (i = 0; i < SLE_TREE_MAX_CHILDREN; i++) {
        sle_tree_child_link_t *child = &g_sle_tree_ctx.children[i];

        if (!child->in_use || child->direct_node_id == SLE_TREE_INVALID_NODE_ID || child->direct_role == 0) {
            continue;
        }
        sle_tree_put_le16(&payload[payload_len], child->direct_node_id);
        payload[payload_len + 2] = child->direct_role;
        payload_len = (uint16_t)(payload_len + 3U);
        child_count++;
    }
    payload[2] = child_count;
    frame_len = sle_tree_build_frame(SLE_TREE_FRAME_TYPE_TOPO_SUMMARY, g_sle_tree_ctx.uplink.root_node_id,
        payload, payload_len, frame, sizeof(frame));
    if (frame_len > 0) {
        sle_tree_uart_printf("%s topo summary sent children=%u\r\n", SLE_TREE_SERVER_LOG_PREFIX, child_count);
        (void)sle_tree_send_uplink_write(frame, frame_len);
    }
}

/*--------------------------------------------------------------------------
 * Uplink frame queue: buffer frames when relay uplink is unavailable.
 * 上行帧队列：relay uplink 不可用时暂存待转发帧。
 *--------------------------------------------------------------------------*/

void sle_tree_frame_queue_enqueue(const uint8_t *data, uint16_t len)
{
    sle_tree_frame_queue_t *q = &g_sle_tree_ctx.frame_queue;

    if (data == NULL || len == 0 || len > SLE_TREE_MAX_FRAME_LEN) {
        return;
    }
    if (q->count >= SLE_TREE_FRAME_QUEUE_LEN) {
        /* 队列满，丢弃最旧帧 */
        sle_tree_uart_printf("%s frame queue full, drop oldest\r\n", SLE_TREE_SERVER_LOG_PREFIX);
        q->head = (uint8_t)((q->head + 1U) % SLE_TREE_FRAME_QUEUE_LEN);
        q->count--;
    }
    (void)memcpy_s(q->entries[q->tail].data, SLE_TREE_MAX_FRAME_LEN, data, len);
    q->entries[q->tail].len = len;
    q->tail = (uint8_t)((q->tail + 1U) % SLE_TREE_FRAME_QUEUE_LEN);
    q->count++;
    sle_tree_uart_printf("%s frame enqueued len=%u queue=%u\r\n", SLE_TREE_SERVER_LOG_PREFIX, len, q->count);
}

bool sle_tree_frame_queue_dequeue(uint8_t *buf, uint16_t *len)
{
    sle_tree_frame_queue_t *q = &g_sle_tree_ctx.frame_queue;

    if (q->count == 0 || buf == NULL || len == NULL) {
        return false;
    }
    *len = q->entries[q->head].len;
    (void)memcpy_s(buf, SLE_TREE_MAX_FRAME_LEN, q->entries[q->head].data, *len);
    q->head = (uint8_t)((q->head + 1U) % SLE_TREE_FRAME_QUEUE_LEN);
    q->count--;
    return true;
}

void sle_tree_frame_queue_flush(void)
{
    sle_tree_frame_queue_t *q = &g_sle_tree_ctx.frame_queue;
    uint8_t buf[SLE_TREE_MAX_FRAME_LEN];
    uint16_t len;
    uint8_t sent = 0;

    while (q->count > 0 && sent < 4) {
        if (!sle_tree_frame_queue_dequeue(buf, &len)) {
            break;
        }
        if (sle_tree_send_uplink_write(buf, len) != ERRCODE_SUCC) {
            break;
        }
        sent++;
    }
    if (sent > 0) {
        sle_tree_uart_printf("%s frame queue flushed=%u remaining=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, sent, q->count);
    }
}

/**
 * @brief Handle a frame whose destination is the local node itself.
 * @brief 处理目标就是本机节点的报文，当前只负责心跳和普通数据打印。
 */
void sle_tree_handle_frame_to_self(const sle_tree_frame_view_t *frame, uint16_t via_conn_id)
{
    if (frame == NULL) {
        return;
    }
    if (frame->frame_type == SLE_TREE_FRAME_TYPE_HEARTBEAT) {
        sle_tree_report_heartbeat(frame->src_node_id, via_conn_id);
        return;
    }
    if (frame->frame_type == SLE_TREE_FRAME_TYPE_DATA) {
        sle_tree_loss_update(frame->src_node_id, frame->seq);
        sle_tree_report_data(frame->src_node_id, frame->payload, frame->payload_len);
    }
}

/* -------------------------------------------------------------------------- */
/* Packet loss rate tracking / 丢包率统计                                     */
/* -------------------------------------------------------------------------- */

static sle_tree_loss_stat_t *sle_tree_find_or_alloc_loss_stat(uint16_t src_node_id)
{
    uint8_t i;
    sle_tree_loss_stat_t *oldest = NULL;

    for (i = 0; i < SLE_TREE_MAX_LOSS_STATS; i++) {
        if (g_sle_tree_ctx.loss_stats[i].in_use &&
            g_sle_tree_ctx.loss_stats[i].src_node_id == src_node_id) {
            return &g_sle_tree_ctx.loss_stats[i];
        }
    }
    for (i = 0; i < SLE_TREE_MAX_LOSS_STATS; i++) {
        if (!g_sle_tree_ctx.loss_stats[i].in_use) {
            oldest = &g_sle_tree_ctx.loss_stats[i];
            (void)memset_s(oldest, sizeof(*oldest), 0, sizeof(*oldest));
            oldest->in_use = true;
            oldest->src_node_id = src_node_id;
            return oldest;
        }
    }
    for (i = 0; i < SLE_TREE_MAX_LOSS_STATS; i++) {
        if (oldest == NULL || g_sle_tree_ctx.loss_stats[i].received_count < oldest->received_count) {
            oldest = &g_sle_tree_ctx.loss_stats[i];
        }
    }
    if (oldest != NULL) {
        (void)memset_s(oldest, sizeof(*oldest), 0, sizeof(*oldest));
        oldest->in_use = true;
        oldest->src_node_id = src_node_id;
    }
    return oldest;
}

void sle_tree_loss_update(uint16_t src_node_id, uint16_t seq)
{
    sle_tree_loss_stat_t *stat;
    uint16_t expected;
    uint16_t gap;

    if (src_node_id == SLE_TREE_INVALID_NODE_ID) {
        return;
    }
    stat = sle_tree_find_or_alloc_loss_stat(src_node_id);
    if (stat == NULL) {
        return;
    }
    stat->received_count++;
    if (!stat->seq_initialized) {
        stat->last_seq = seq;
        stat->seq_initialized = true;
        return;
    }
    expected = (uint16_t)(stat->last_seq + 1U);
    if (seq == expected) {
        stat->last_seq = seq;
    } else if (seq > expected) {
        gap = (uint16_t)(seq - expected);
        stat->lost_count += gap;
        stat->last_seq = seq;
    } else {
        stat->out_of_order_count++;
    }
}

void sle_tree_loss_report(void)
{
    uint8_t i;
    bool has_data = false;
    uint32_t total_expected;
    uint32_t loss_permille;

    for (i = 0; i < SLE_TREE_MAX_LOSS_STATS; i++) {
        sle_tree_loss_stat_t *stat = &g_sle_tree_ctx.loss_stats[i];
        if (!stat->in_use || stat->received_count == 0) {
            continue;
        }
        if (!has_data) {
            sle_tree_uart_printf("%s LOSS_REPORT begin\r\n", SLE_TREE_SERVER_LOG_PREFIX);
            has_data = true;
        }
        total_expected = stat->received_count + stat->lost_count;
        loss_permille = (total_expected > 0) ?
            (uint32_t)((uint64_t)stat->lost_count * 1000U / total_expected) : 0;
        sle_tree_uart_printf("%s LOSS src=%u recv=%u lost=%u ooo=%u rate=%u.%u%%\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, stat->src_node_id,
            stat->received_count, stat->lost_count, stat->out_of_order_count,
            loss_permille / 10U, loss_permille % 10U);
    }
    if (has_data) {
        sle_tree_uart_printf("%s LOSS_REPORT end\r\n", SLE_TREE_SERVER_LOG_PREFIX);
    }
}

void sle_tree_loss_reset(void)
{
    (void)memset_s(g_sle_tree_ctx.loss_stats, sizeof(g_sle_tree_ctx.loss_stats),
        0, sizeof(g_sle_tree_ctx.loss_stats));
    g_sle_tree_ctx.last_loss_report_ms = sle_tree_now_ms();
    sle_tree_uart_printf("%s LOSS stats reset\r\n", SLE_TREE_SERVER_LOG_PREFIX);
}
