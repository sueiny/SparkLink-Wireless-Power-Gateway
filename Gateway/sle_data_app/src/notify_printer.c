#include "notify_printer.h"
#include "ipc_sender.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NOTIFY_ASCII_LIMIT  96
#define SLE_APP_LOG_PATH    "/tmp/sle_app.log"
#define NOTIFY_QUEUE_CAPACITY 1024
#define NOTIFY_PAYLOAD_MAX  1500

/* ==========================================================================
 * 内部队列数据结构
 * ========================================================================== */

typedef struct {
    int server_index;
    uint16_t conn_id;
    sle_addr_t addr;
    uint16_t len;
    uint32_t rx_count;
    uint8_t data[NOTIFY_PAYLOAD_MAX];
} notify_packet_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    FILE *log_fp;
    notify_packet_t queue[NOTIFY_QUEUE_CAPACITY];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint32_t dropped;
    bool running;
    bool accepting;
    bool initialized;
    bool thread_started;
} notify_printer_ctx_t;

static notify_printer_ctx_t g_ctx;

/* ==========================================================================
 * 队列操作（内部）
 * ========================================================================== */

static bool queue_push(int server_index, const sle_server_connection_t *conn,
    const uint8_t *data, uint16_t len)
{
    if (conn == NULL || data == NULL || len == 0 || !g_ctx.initialized) {
        return false;
    }

    uint16_t copy_len = len > NOTIFY_PAYLOAD_MAX ? NOTIFY_PAYLOAD_MAX : len;

    pthread_mutex_lock(&g_ctx.mutex);
    if (!g_ctx.accepting || g_ctx.count >= NOTIFY_QUEUE_CAPACITY) {
        if (g_ctx.dropped < UINT32_MAX) {
            g_ctx.dropped++;
        }
        pthread_mutex_unlock(&g_ctx.mutex);
        return false;
    }

    notify_packet_t *pkt = &g_ctx.queue[g_ctx.tail];
    pkt->server_index = server_index;
    pkt->conn_id = conn->conn_id;
    pkt->addr = conn->addr;
    pkt->len = copy_len;
    pkt->rx_count = conn->rx_count;
    memcpy(pkt->data, data, copy_len);
    g_ctx.tail = (uint16_t)((g_ctx.tail + 1) % NOTIFY_QUEUE_CAPACITY);
    g_ctx.count++;
    pthread_cond_signal(&g_ctx.cond);
    pthread_mutex_unlock(&g_ctx.mutex);
    return true;
}

static bool queue_pop(notify_packet_t *out, uint32_t *out_dropped)
{
    pthread_mutex_lock(&g_ctx.mutex);
    while (g_ctx.running && g_ctx.count == 0) {
        pthread_cond_wait(&g_ctx.cond, &g_ctx.mutex);
    }

    if (!g_ctx.running && g_ctx.count == 0) {
        pthread_mutex_unlock(&g_ctx.mutex);
        return false;
    }

    *out = g_ctx.queue[g_ctx.head];
    g_ctx.head = (uint16_t)((g_ctx.head + 1) % NOTIFY_QUEUE_CAPACITY);
    g_ctx.count--;
    *out_dropped = g_ctx.dropped;
    g_ctx.dropped = 0;
    pthread_mutex_unlock(&g_ctx.mutex);
    return true;
}

/* ==========================================================================
 * 日志格式化与输出
 * ========================================================================== */

static void print_drop_warning(FILE *stream, uint32_t dropped)
{
    if (stream == NULL || dropped == 0) {
        return;
    }
    fprintf(stream, "[SLE][WARN] notify queue dropped=%u\n", dropped);
    fflush(stream);
}

static void print_packet(FILE *stream, const notify_packet_t *pkt)
{
    if (stream == NULL || pkt == NULL) {
        return;
    }

    char addr[32] = {0};
    server_connections_addr_to_string(&pkt->addr, addr, sizeof(addr));

    fprintf(stream, "[SLE][RX] server_index=%d conn_id=%u mac=%s len=%u rx_count=%u hex=",
        pkt->server_index, pkt->conn_id, addr, pkt->len, pkt->rx_count);
    for (uint16_t i = 0; i < pkt->len; ++i) {
        fprintf(stream, "%02x", pkt->data[i]);
        if (i + 1 < pkt->len) {
            fprintf(stream, " ");
        }
    }

    fprintf(stream, " ascii=\"");
    uint16_t ascii_len = pkt->len < NOTIFY_ASCII_LIMIT ? pkt->len : NOTIFY_ASCII_LIMIT;
    for (uint16_t i = 0; i < ascii_len; ++i) {
        unsigned char ch = pkt->data[i];
        fputc(isprint(ch) ? ch : '.', stream);
    }
    if (pkt->len > NOTIFY_ASCII_LIMIT) {
        fprintf(stream, "...");
    }
    fprintf(stream, "\"\n");
    fflush(stream);
}

/* ==========================================================================
 * 消费线程
 * ========================================================================== */

static void *log_worker(void *arg)
{
    (void)arg;

    for (;;) {
        notify_packet_t pkt;
        uint32_t dropped = 0;
        if (!queue_pop(&pkt, &dropped)) {
            break;
        }

        if (dropped > 0) {
            print_drop_warning(stderr, dropped);
            print_drop_warning(g_ctx.log_fp, dropped);
        }
        print_packet(stderr, &pkt);
        print_packet(g_ctx.log_fp, &pkt);

        /* 发送到 gatewayd IPC 通道 */
        ipc_sender_send_raw(pkt.data, pkt.len);
    }

    /* drain 剩余数据 */
    notify_packet_t pkt;
    uint32_t dropped = 0;
    while (g_ctx.running || g_ctx.count > 0) {
        pthread_mutex_lock(&g_ctx.mutex);
        if (g_ctx.count == 0) {
            pthread_mutex_unlock(&g_ctx.mutex);
            break;
        }
        pthread_mutex_unlock(&g_ctx.mutex);
        if (!queue_pop(&pkt, &dropped)) {
            break;
        }
        if (dropped > 0) {
            print_drop_warning(stderr, dropped);
            print_drop_warning(g_ctx.log_fp, dropped);
            dropped = 0;
        }
        print_packet(stderr, &pkt);
        print_packet(g_ctx.log_fp, &pkt);

        ipc_sender_send_raw(pkt.data, pkt.len);
    }
    return NULL;
}

/* ==========================================================================
 * 公开接口
 * ========================================================================== */

int notify_printer_start(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
    if (pthread_mutex_init(&g_ctx.mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&g_ctx.cond, NULL) != 0) {
        pthread_mutex_destroy(&g_ctx.mutex);
        return -1;
    }
    g_ctx.initialized = true;
    g_ctx.log_fp = fopen(SLE_APP_LOG_PATH, "w");
    if (g_ctx.log_fp == NULL) {
        fprintf(stderr, "[SLE][WARN] open notify log failed path=%s\n", SLE_APP_LOG_PATH);
    }
    g_ctx.running = true;
    g_ctx.accepting = true;

    if (pthread_create(&g_ctx.thread, NULL, log_worker, NULL) != 0) {
        g_ctx.running = false;
        g_ctx.accepting = false;
        if (g_ctx.log_fp != NULL) {
            fclose(g_ctx.log_fp);
            g_ctx.log_fp = NULL;
        }
        pthread_cond_destroy(&g_ctx.cond);
        pthread_mutex_destroy(&g_ctx.mutex);
        memset(&g_ctx, 0, sizeof(g_ctx));
        return -1;
    }
    g_ctx.thread_started = true;
    return 0;
}

void notify_printer_stop(void)
{
    if (!g_ctx.initialized) {
        return;
    }

    pthread_mutex_lock(&g_ctx.mutex);
    g_ctx.accepting = false;
    g_ctx.running = false;
    pthread_cond_signal(&g_ctx.cond);
    pthread_mutex_unlock(&g_ctx.mutex);

    if (g_ctx.thread_started) {
        pthread_join(g_ctx.thread, NULL);
        g_ctx.thread_started = false;
    }

    if (g_ctx.log_fp != NULL) {
        fclose(g_ctx.log_fp);
        g_ctx.log_fp = NULL;
    }
    pthread_cond_destroy(&g_ctx.cond);
    pthread_mutex_destroy(&g_ctx.mutex);
    memset(&g_ctx, 0, sizeof(g_ctx));
}

bool notify_printer_enqueue_packet(int server_index, const sle_server_connection_t *conn,
    const uint8_t *data, uint16_t len)
{
    return queue_push(server_index, conn, data, len);
}
