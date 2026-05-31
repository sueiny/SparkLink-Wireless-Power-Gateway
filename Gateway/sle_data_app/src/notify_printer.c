#include "notify_printer.h"

#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define NOTIFY_ASCII_LIMIT 96
#define SLE_APP_LOG_PATH "/tmp/sle_app.log"
#define NOTIFY_QUEUE_CAPACITY 1024
#define NOTIFY_PAYLOAD_MAX 1500

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
} notify_printer_context_t;

static notify_printer_context_t g_notify_printer;

static int notify_log_worker_start(void);
static void notify_log_worker_join(void);

static void print_drop_warning_locked(FILE *stream, uint32_t dropped)
{
    if (stream == NULL || dropped == 0) {
        return;
    }
    fprintf(stream, "[SLE][WARN] notify queue dropped=%u\n", dropped);
    fflush(stream);
}

static void print_drop_warning(uint32_t dropped)
{
    print_drop_warning_locked(stderr, dropped);
    print_drop_warning_locked(g_notify_printer.log_fp, dropped);
}

static void print_packet_to_stream(FILE *stream, const notify_packet_t *packet)
{
    if (stream == NULL || packet == NULL) {
        return;
    }

    char addr[32] = {0};
    server_connections_addr_to_string(&packet->addr, addr, sizeof(addr));

    fprintf(stream, "[SLE][RX] server_index=%d conn_id=%u mac=%s len=%u rx_count=%u hex=",
        packet->server_index, packet->conn_id, addr, packet->len, packet->rx_count);
    for (uint16_t i = 0; i < packet->len; ++i) {
        fprintf(stream, "%02x", packet->data[i]);
        if (i + 1 < packet->len) {
            fprintf(stream, " ");
        }
    }

    fprintf(stream, " ascii=\"");
    uint16_t ascii_len = packet->len < NOTIFY_ASCII_LIMIT ? packet->len : NOTIFY_ASCII_LIMIT;
    for (uint16_t i = 0; i < ascii_len; ++i) {
        unsigned char ch = packet->data[i];
        fputc(isprint(ch) ? ch : '.', stream);
    }
    if (packet->len > NOTIFY_ASCII_LIMIT) {
        fprintf(stream, "...");
    }
    fprintf(stream, "\"\n");
    fflush(stream);
}

static bool dequeue_packet(notify_packet_t *packet, uint32_t *dropped)
{
    pthread_mutex_lock(&g_notify_printer.mutex);
    while (g_notify_printer.running && g_notify_printer.count == 0) {
        pthread_cond_wait(&g_notify_printer.cond, &g_notify_printer.mutex);
    }

    if (!g_notify_printer.running && g_notify_printer.count == 0) {
        pthread_mutex_unlock(&g_notify_printer.mutex);
        return false;
    }

    *packet = g_notify_printer.queue[g_notify_printer.head];
    g_notify_printer.head = (uint16_t)((g_notify_printer.head + 1) % NOTIFY_QUEUE_CAPACITY);
    g_notify_printer.count--;
    *dropped = g_notify_printer.dropped;
    g_notify_printer.dropped = 0;
    pthread_mutex_unlock(&g_notify_printer.mutex);
    return true;
}

/* Formats queued notify packets and mirrors them to stderr and /tmp/sle_app.log. */
static void *notify_log_worker(void *arg)
{
    (void)arg;

    for (;;) {
        notify_packet_t packet;
        uint32_t dropped = 0;
        if (!dequeue_packet(&packet, &dropped)) {
            break;
        }

        print_drop_warning(dropped);
        print_packet_to_stream(stderr, &packet);
        print_packet_to_stream(g_notify_printer.log_fp, &packet);
    }

    pthread_mutex_lock(&g_notify_printer.mutex);
    uint32_t dropped = g_notify_printer.dropped;
    g_notify_printer.dropped = 0;
    pthread_mutex_unlock(&g_notify_printer.mutex);
    print_drop_warning(dropped);
    return NULL;
}

int notify_printer_start(void)
{
    memset(&g_notify_printer, 0, sizeof(g_notify_printer));
    if (pthread_mutex_init(&g_notify_printer.mutex, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&g_notify_printer.cond, NULL) != 0) {
        pthread_mutex_destroy(&g_notify_printer.mutex);
        return -1;
    }
    g_notify_printer.initialized = true;
    g_notify_printer.log_fp = fopen(SLE_APP_LOG_PATH, "w");
    if (g_notify_printer.log_fp == NULL) {
        fprintf(stderr, "[SLE][WARN] open notify log failed path=%s\n", SLE_APP_LOG_PATH);
    }
    g_notify_printer.running = true;
    g_notify_printer.accepting = true;
    if (notify_log_worker_start() != 0) {
        g_notify_printer.running = false;
        g_notify_printer.accepting = false;
        if (g_notify_printer.log_fp != NULL) {
            fclose(g_notify_printer.log_fp);
            g_notify_printer.log_fp = NULL;
        }
        pthread_cond_destroy(&g_notify_printer.cond);
        pthread_mutex_destroy(&g_notify_printer.mutex);
        memset(&g_notify_printer, 0, sizeof(g_notify_printer));
        return -1;
    }
    return 0;
}

void notify_printer_stop(void)
{
    if (!g_notify_printer.initialized) {
        return;
    }

    pthread_mutex_lock(&g_notify_printer.mutex);
    g_notify_printer.accepting = false;
    g_notify_printer.running = false;
    pthread_cond_signal(&g_notify_printer.cond);
    pthread_mutex_unlock(&g_notify_printer.mutex);

    notify_log_worker_join();

    if (g_notify_printer.log_fp != NULL) {
        fclose(g_notify_printer.log_fp);
        g_notify_printer.log_fp = NULL;
    }
    pthread_cond_destroy(&g_notify_printer.cond);
    pthread_mutex_destroy(&g_notify_printer.mutex);
    memset(&g_notify_printer, 0, sizeof(g_notify_printer));
}

bool notify_printer_enqueue_packet(int server_index, const sle_server_connection_t *conn,
    const uint8_t *data, uint16_t len)
{
    if (conn == NULL || data == NULL || len == 0 || !g_notify_printer.initialized) {
        return false;
    }

    uint16_t copy_len = len;
    if (copy_len > NOTIFY_PAYLOAD_MAX) {
        copy_len = NOTIFY_PAYLOAD_MAX;
    }

    pthread_mutex_lock(&g_notify_printer.mutex);
    if (!g_notify_printer.accepting || g_notify_printer.count >= NOTIFY_QUEUE_CAPACITY) {
        if (g_notify_printer.dropped < UINT32_MAX) {
            g_notify_printer.dropped++;
        }
        pthread_mutex_unlock(&g_notify_printer.mutex);
        return false;
    }

    notify_packet_t *packet = &g_notify_printer.queue[g_notify_printer.tail];
    packet->server_index = server_index;
    packet->conn_id = conn->conn_id;
    packet->addr = conn->addr;
    packet->len = copy_len;
    packet->rx_count = conn->rx_count;
    memcpy(packet->data, data, copy_len);
    g_notify_printer.tail = (uint16_t)((g_notify_printer.tail + 1) % NOTIFY_QUEUE_CAPACITY);
    g_notify_printer.count++;
    pthread_cond_signal(&g_notify_printer.cond);
    pthread_mutex_unlock(&g_notify_printer.mutex);
    return true;
}

/* --------------------------------------------------------------------------
 * Thread entry points
 * -------------------------------------------------------------------------- */

static int notify_log_worker_start(void)
{
    int ret = pthread_create(&g_notify_printer.thread, NULL, notify_log_worker, NULL);
    if (ret != 0) {
        return -1;
    }
    g_notify_printer.thread_started = true;
    return 0;
}

static void notify_log_worker_join(void)
{
    if (!g_notify_printer.thread_started) {
        return;
    }
    pthread_join(g_notify_printer.thread, NULL);
    g_notify_printer.thread_started = false;
}
