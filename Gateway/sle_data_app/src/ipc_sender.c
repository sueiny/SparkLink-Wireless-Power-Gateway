#include "ipc_sender.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

#define SOCKET_PATH            "/var/run/gateway/sle_data.sock"
#define RECONNECT_INTERVAL_MS  3000

static int g_fd = -1;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_last_reconnect_ms;

static uint64_t get_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static void ensure_dir(void)
{
    mkdir("/var/run", 0755);
    mkdir("/var/run/gateway", 0755);
}

static int try_connect(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SOCKET_PATH);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    g_fd = fd;
    return 0;
}

int ipc_sender_init(void)
{
    ensure_dir();
    g_fd = -1;
    g_last_reconnect_ms = 0;
    fprintf(stderr, "[IPC] sender initialized\n");
    return 0;
}

void ipc_sender_deinit(void)
{
    pthread_mutex_lock(&g_mutex);
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
    pthread_mutex_unlock(&g_mutex);
}

bool ipc_sender_send_raw(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return false;
    }

    pthread_mutex_lock(&g_mutex);

    if (g_fd < 0) {
        uint64_t now = get_now_ms();
        if (now - g_last_reconnect_ms < RECONNECT_INTERVAL_MS) {
            pthread_mutex_unlock(&g_mutex);
            return false;
        }
        g_last_reconnect_ms = now;
        if (try_connect() != 0) {
            pthread_mutex_unlock(&g_mutex);
            return false;
        }
        fprintf(stderr, "[IPC] connected to gatewayd\n");
    }

    /* 写 2 字节 LE 长度前缀 + 原始帧数据 */
    uint8_t len_prefix[2];
    len_prefix[0] = (uint8_t)(len & 0xFF);
    len_prefix[1] = (uint8_t)((len >> 8) & 0xFF);

    bool ok = true;
    /* 先写长度前缀 */
    size_t sent = 0;
    while (sent < 2) {
        ssize_t n = write(g_fd, len_prefix + sent, 2 - sent);
        if (n <= 0) { ok = false; break; }
        sent += (size_t)n;
    }
    /* 再写原始帧数据 */
    if (ok) {
        sent = 0;
        while (sent < len) {
            ssize_t n = write(g_fd, data + sent, len - sent);
            if (n <= 0) { ok = false; break; }
            sent += (size_t)n;
        }
    }

    if (!ok) {
        close(g_fd);
        g_fd = -1;
        g_last_reconnect_ms = get_now_ms();
    }

    pthread_mutex_unlock(&g_mutex);
    return ok;
}
