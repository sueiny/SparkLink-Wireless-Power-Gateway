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
#define NOTIFY_REASSEMBLY_SLOTS 8
#define NOTIFY_HEX_LINE_MAX 1024

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
    bool active;
    int server_index;
    uint16_t conn_id;
    uint8_t hex[NOTIFY_HEX_LINE_MAX];
    uint16_t hex_len;
} ascii_hex_reassembly_t;

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

static int hex_value(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static bool is_space_byte(uint8_t ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static bool is_printable_ascii_packet(const notify_packet_t *pkt)
{
    if (pkt == NULL || pkt->len == 0) {
        return false;
    }
    for (uint16_t i = 0; i < pkt->len; ++i) {
        uint8_t ch = pkt->data[i];
        if (!(isprint(ch) || is_space_byte(ch))) {
            return false;
        }
    }
    return true;
}

static bool normalize_ascii_hex_fragment(const uint8_t *data, uint16_t len,
    bool allow_dst_prefix, uint8_t *hex_out, uint16_t *hex_len, bool *starts_with_st)
{
    if (data == NULL || hex_out == NULL || hex_len == NULL || starts_with_st == NULL) {
        return false;
    }

    *hex_len = 0;
    *starts_with_st = false;
    size_t start = 0;
    size_t end = len;
    while (start < end && is_space_byte(data[start])) {
        start++;
    }
    while (end > start && is_space_byte(data[end - 1])) {
        end--;
    }
    if (end <= start) {
        return false;
    }

    if (allow_dst_prefix) {
        size_t p = start;
        while (p < end && data[p] >= '0' && data[p] <= '9') {
            p++;
        }
        if (p > start && p < end && is_space_byte(data[p])) {
            while (p < end && is_space_byte(data[p])) {
                p++;
            }
            if (p < end) {
                start = p;
            }
        }
    }

    for (size_t i = start; i < end; ++i) {
        if (is_space_byte(data[i])) {
            continue;
        }
        if (hex_value(data[i]) < 0) {
            return false;
        }
        if (*hex_len >= NOTIFY_HEX_LINE_MAX) {
            return false;
        }
        hex_out[(*hex_len)++] = data[i];
    }

    if (*hex_len == 0) {
        return false;
    }
    if (*hex_len >= 4 &&
        hex_value(hex_out[0]) == 5 && hex_value(hex_out[1]) == 3 &&
        hex_value(hex_out[2]) == 5 && hex_value(hex_out[3]) == 4) {
        *starts_with_st = true;
    }
    return true;
}

static bool decode_hex_digits(const uint8_t *hex, uint16_t hex_len,
    uint8_t *out, uint16_t *out_len)
{
    if (hex == NULL || out == NULL || out_len == NULL || (hex_len % 2) != 0) {
        return false;
    }

    uint16_t decoded_len = 0;
    int high = -1;
    for (uint16_t i = 0; i < hex_len; ++i) {
        int v = hex_value(hex[i]);
        if (v < 0) {
            return false;
        }
        if (high < 0) {
            high = v;
            continue;
        }
        if (decoded_len >= NOTIFY_PAYLOAD_MAX) {
            return false;
        }
        out[decoded_len++] = (uint8_t)((high << 4) | v);
        high = -1;
    }
    *out_len = decoded_len;
    return true;
}

static bool decoded_st_frame_complete(const uint8_t *decoded, uint16_t decoded_len,
    uint16_t *frame_len)
{
    if (decoded == NULL || frame_len == NULL || decoded_len < 13) {
        return false;
    }
    if (decoded[0] != 'S' || decoded[1] != 'T') {
        return false;
    }
    uint16_t payload_len = (uint16_t)(decoded[11] | (decoded[12] << 8));
    uint16_t total_len = (uint16_t)(13 + payload_len);
    if (payload_len > 243 || total_len > NOTIFY_PAYLOAD_MAX) {
        return false;
    }
    if (decoded_len < total_len) {
        *frame_len = total_len;
        return false;
    }
    *frame_len = total_len;
    return true;
}

static bool decode_ascii_hex_st_frame(const uint8_t *hex, uint16_t hex_len,
    uint8_t *out, uint16_t *out_len)
{
    uint16_t decoded_len = 0;
    if (!decode_hex_digits(hex, hex_len, out, &decoded_len)) {
        return false;
    }

    uint16_t frame_len = 0;
    if (!decoded_st_frame_complete(out, decoded_len, &frame_len)) {
        return false;
    }

    *out_len = frame_len;
    return true;
}

static ascii_hex_reassembly_t *get_reassembly_slot(ascii_hex_reassembly_t *slots, const notify_packet_t *pkt)
{
    if (slots == NULL || pkt == NULL) {
        return NULL;
    }
    int idx = pkt->server_index;
    if (idx < 0 || idx >= NOTIFY_REASSEMBLY_SLOTS) {
        idx = 0;
    }
    ascii_hex_reassembly_t *slot = &slots[idx];
    if (slot->active &&
        (slot->server_index != pkt->server_index || slot->conn_id != pkt->conn_id)) {
        slot->active = false;
        slot->hex_len = 0;
    }
    slot->server_index = pkt->server_index;
    slot->conn_id = pkt->conn_id;
    return slot;
}

typedef enum {
    IPC_PREP_SKIP = 0,
    IPC_PREP_READY,
    IPC_PREP_DECODED
} ipc_prep_result_t;

static ipc_prep_result_t prepare_ipc_packet(const notify_packet_t *raw, notify_packet_t *ipc_pkt,
    ascii_hex_reassembly_t *slots)
{
    if (raw == NULL || ipc_pkt == NULL) {
        return IPC_PREP_SKIP;
    }
    *ipc_pkt = *raw;

    if (raw->len >= 2 && raw->data[0] == 'S' && raw->data[1] == 'T') {
        return IPC_PREP_READY;
    }

    ascii_hex_reassembly_t *slot = get_reassembly_slot(slots, raw);
    uint8_t fragment[NOTIFY_HEX_LINE_MAX];
    uint16_t fragment_len = 0;
    bool starts_with_st = false;
    bool is_hex = normalize_ascii_hex_fragment(raw->data, raw->len, !slot->active,
        fragment, &fragment_len, &starts_with_st);

    if (is_hex && (slot->active || starts_with_st)) {
        if (starts_with_st) {
            slot->active = true;
            slot->hex_len = 0;
        }
        if (slot->hex_len + fragment_len > NOTIFY_HEX_LINE_MAX) {
            slot->active = false;
            slot->hex_len = 0;
            return IPC_PREP_SKIP;
        }
        memcpy(slot->hex + slot->hex_len, fragment, fragment_len);
        slot->hex_len = (uint16_t)(slot->hex_len + fragment_len);

        uint8_t decoded[NOTIFY_PAYLOAD_MAX];
        uint16_t decoded_len = 0;
        if (!decode_ascii_hex_st_frame(slot->hex, slot->hex_len, decoded, &decoded_len)) {
            return IPC_PREP_SKIP;
        }

        memcpy(ipc_pkt->data, decoded, decoded_len);
        ipc_pkt->len = decoded_len;
        slot->active = false;
        slot->hex_len = 0;
        return IPC_PREP_DECODED;
    }

    if (is_printable_ascii_packet(raw)) {
        return IPC_PREP_SKIP;
    }

    return IPC_PREP_READY;
}

static bool prepare_ipc_packet_legacy(const notify_packet_t *raw, notify_packet_t *ipc_pkt)
{
    if (raw == NULL || ipc_pkt == NULL) {
        return false;
    }
    *ipc_pkt = *raw;

    uint8_t decoded[NOTIFY_PAYLOAD_MAX];
    uint16_t decoded_len = 0;
    uint8_t fragment[NOTIFY_HEX_LINE_MAX];
    uint16_t fragment_len = 0;
    bool starts_with_st = false;
    if (!normalize_ascii_hex_fragment(raw->data, raw->len, true,
        fragment, &fragment_len, &starts_with_st) || !starts_with_st ||
        !decode_ascii_hex_st_frame(fragment, fragment_len, decoded, &decoded_len)) {
        return false;
    }

    memcpy(ipc_pkt->data, decoded, decoded_len);
    ipc_pkt->len = decoded_len;
    return true;
}

static void print_decode_notice(FILE *stream, const notify_packet_t *raw, const notify_packet_t *decoded)
{
    if (stream == NULL || raw == NULL || decoded == NULL) {
        return;
    }
    fprintf(stream, "[SLE][DECODED] server_index=%d conn_id=%u rx_count=%u ascii_hex_len=%u st_len=%u magic=%02x %02x\n",
        raw->server_index, raw->conn_id, raw->rx_count, raw->len, decoded->len,
        decoded->data[0], decoded->data[1]);
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
    ascii_hex_reassembly_t reassembly[NOTIFY_REASSEMBLY_SLOTS];
    int batch_count = 0;
    int64_t batch_start_ms = 0;

    memset(reassembly, 0, sizeof(reassembly));

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

        notify_packet_t ipc_pkt;
        ipc_prep_result_t prep = prepare_ipc_packet(&pkt, &ipc_pkt, reassembly);
        if (prep == IPC_PREP_SKIP) {
            continue;
        }
        if (prep == IPC_PREP_DECODED) {
            print_decode_notice(stderr, &pkt, &ipc_pkt);
            print_decode_notice(g_ctx.log_fp, &pkt, &ipc_pkt);
        }

        /* 添加到批量缓冲区 */
        batch_packets[batch_count] = ipc_pkt;
        batch_frames[batch_count].data = batch_packets[batch_count].data;
        batch_frames[batch_count].len = ipc_pkt.len;
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

        notify_packet_t ipc_pkt;
        bool decoded = prepare_ipc_packet_legacy(&pkt, &ipc_pkt);
        if (decoded) {
            print_decode_notice(stderr, &pkt, &ipc_pkt);
            print_decode_notice(g_ctx.log_fp, &pkt, &ipc_pkt);
        } else {
            ipc_pkt = pkt;
        }

        ipc_sender_send_raw(ipc_pkt.data, ipc_pkt.len);
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
