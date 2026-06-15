#include "notify_printer.h"
#include "ipc_sender.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NOTIFY_ASCII_LIMIT  96
#define SLE_APP_LOG_PATH    "/tmp/sle_app.log"
#define NOTIFY_QUEUE_CAPACITY 1024
#define NOTIFY_PAYLOAD_MAX  1500
#define NOTIFY_BATCH_MAX    64
#define NOTIFY_BATCH_FLUSH_MS 1000

/*
 * notify_printer 是 SLE/mock 回调和 gatewayd 数据 IPC 之间的缓冲层。
 * 回调线程只复制数据入队，log_worker 负责日志输出和批量发送，避免 SDK 回调
 * 被 printf、文件 I/O 或 socket 写阻塞。
 */

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
    atomic_bool running;
    bool initialized;
    bool thread_started;
} notify_printer_ctx_t;

static notify_printer_ctx_t g_ctx;

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}

static void make_abs_timeout(struct timespec *ts, int timeout_ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += timeout_ms / 1000;
    ts->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

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
    if (!atomic_load(&g_ctx.running) || g_ctx.count >= NOTIFY_QUEUE_CAPACITY) {
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

static bool queue_pop_wait(notify_packet_t *out, uint32_t *out_dropped, int timeout_ms)
{
    pthread_mutex_lock(&g_ctx.mutex);
    while (atomic_load(&g_ctx.running) && g_ctx.count == 0) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&g_ctx.cond, &g_ctx.mutex);
        } else {
            struct timespec timeout_at;
            make_abs_timeout(&timeout_at, timeout_ms);
            int rc = pthread_cond_timedwait(&g_ctx.cond, &g_ctx.mutex, &timeout_at);
            if (rc == ETIMEDOUT && g_ctx.count == 0) {
                pthread_mutex_unlock(&g_ctx.mutex);
                return false;
            }
        }
    }

    if (!atomic_load(&g_ctx.running) && g_ctx.count == 0) {
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

static bool queue_pop(notify_packet_t *out, uint32_t *out_dropped)
{
    return queue_pop_wait(out, out_dropped, -1);
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

    /* 构建 hex 字符串到缓冲区，减少 fprintf 调用次数 */
    char hex_buf[NOTIFY_PAYLOAD_MAX * 3 + 1];  /* 每字节 2 hex + 空格 + NUL */
    uint16_t hex_pos = 0;
    uint16_t print_len = pkt->len < NOTIFY_PAYLOAD_MAX ? pkt->len : NOTIFY_PAYLOAD_MAX;
    for (uint16_t i = 0; i < print_len; ++i) {
        hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02x", pkt->data[i]);
        if (i + 1 < print_len && hex_pos < sizeof(hex_buf) - 1) {
            hex_buf[hex_pos++] = ' ';
        }
    }
    hex_buf[hex_pos] = '\0';

    /* 构建 ascii 字符串 */
    char ascii_buf[NOTIFY_ASCII_LIMIT + 4 + 1];  /* + "..." + NUL */
    uint16_t ascii_len = pkt->len < NOTIFY_ASCII_LIMIT ? pkt->len : NOTIFY_ASCII_LIMIT;
    for (uint16_t i = 0; i < ascii_len; ++i) {
        unsigned char ch = pkt->data[i];
        ascii_buf[i] = isprint(ch) ? ch : '.';
    }
    if (pkt->len > NOTIFY_ASCII_LIMIT) {
        ascii_buf[ascii_len++] = '.';
        ascii_buf[ascii_len++] = '.';
        ascii_buf[ascii_len++] = '.';
    }
    ascii_buf[ascii_len] = '\0';

    /* 一次 fprintf 输出整行 */
    fprintf(stream, "[SLE][RX] server_index=%d conn_id=%u mac=%s len=%u rx_count=%u hex=%s ascii=\"%s\"\n",
        pkt->server_index, pkt->conn_id, addr, pkt->len, pkt->rx_count, hex_buf, ascii_buf);
}

/* ==========================================================================
 * 消费线程
 * ========================================================================== */

static void *log_worker(void *arg)
{
    (void)arg;

    /*
     * 批量发送缓冲区。
     * 满批优先保证高吞吐；首帧入批后最多等待 1 秒，避免真实低流量
     * DTU 场景因为凑不满批次而长期不上送 gatewayd。
     */
    ipc_frame_t batch_frames[NOTIFY_BATCH_MAX];
    notify_packet_t batch_packets[NOTIFY_BATCH_MAX];
    int batch_count = 0;
    int64_t batch_start_ms = 0;

    for (;;) {
        notify_packet_t pkt;
        uint32_t dropped = 0;
        int wait_ms = -1;

        if (batch_count > 0) {
            int64_t elapsed_ms = now_ms() - batch_start_ms;
            if (elapsed_ms >= NOTIFY_BATCH_FLUSH_MS) {
                goto flush_batch;
            }
            wait_ms = (int)(NOTIFY_BATCH_FLUSH_MS - elapsed_ms);
        }

        if (batch_count < NOTIFY_BATCH_MAX) {
            if (!queue_pop_wait(&pkt, &dropped, wait_ms)) {
                if (batch_count > 0) {
                    goto flush_batch;
                }
                if (!atomic_load(&g_ctx.running)) {
                    break;
                }
                continue;
            }
        } else {
flush_batch:
            if (batch_count > 0) {
                ipc_sender_send_batch(batch_frames, batch_count);
                batch_count = 0;
                batch_start_ms = 0;
            }
            if (!atomic_load(&g_ctx.running)) {
                break;
            }
            continue;
        }

        if (dropped > 0) {
            print_drop_warning(stderr, dropped);
            print_drop_warning(g_ctx.log_fp, dropped);
        }
        print_packet(stderr, &pkt);
        print_packet(g_ctx.log_fp, &pkt);

        /* 添加到批量缓冲区 */
        batch_packets[batch_count] = pkt;
        batch_frames[batch_count].data = batch_packets[batch_count].data;
        batch_frames[batch_count].len = pkt.len;
        batch_count++;
        if (batch_count == 1) {
            batch_start_ms = now_ms();
        }

        if (batch_count >= NOTIFY_BATCH_MAX) {
            if (batch_count > 0) {
                ipc_sender_send_batch(batch_frames, batch_count);
                batch_count = 0;
                batch_start_ms = 0;
            }
        }
    }

    /* drain 剩余数据 */
    if (batch_count > 0) {
        ipc_sender_send_batch(batch_frames, batch_count);
    }

    notify_packet_t pkt;
    uint32_t dropped = 0;
    while (atomic_load(&g_ctx.running) || g_ctx.count > 0) {
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
    atomic_store(&g_ctx.running, true);

    if (pthread_create(&g_ctx.thread, NULL, log_worker, NULL) != 0) {
        atomic_store(&g_ctx.running, false);
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

    atomic_store(&g_ctx.running, false);

    pthread_mutex_lock(&g_ctx.mutex);
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
