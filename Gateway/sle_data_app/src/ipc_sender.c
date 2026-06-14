#include "ipc_sender.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

#define SOCKET_PATH            "/var/run/gateway/sle_data.sock"
#define RECONNECT_INTERVAL_MS  3000
#define MAX_BATCH_FRAMES       64
#define BATCH_BUF_SIZE         (64 * 1024)  /* 64KB 批量缓冲区 */

static int g_fd = -1;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_last_reconnect_ms;

static uint64_t get_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
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

    /* 使用抽象命名空间（以 \0 开头），不需要文件系统支持 */
    const char *path = SOCKET_PATH;
    if (path[0] == '/')
        path++;  /* 去掉开头的 '/' */
    addr.sun_path[0] = '\0';
    snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "%s", path);

    socklen_t addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(path);
    if (connect(fd, (struct sockaddr *)&addr, addrlen) < 0) {
        close(fd);
        return -1;
    }

    g_fd = fd;
    return 0;
}

int ipc_sender_init(void)
{
    // 抽象命名空间不需要创建目录
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

/* 确保连接可用，必要时重连。调用者必须持有 g_mutex。 */
static bool ensure_connected(void)
{
    if (g_fd >= 0) {
        return true;
    }

    uint64_t now = get_now_ms();
    if (now - g_last_reconnect_ms < RECONNECT_INTERVAL_MS) {
        return false;
    }
    g_last_reconnect_ms = now;
    if (try_connect() != 0) {
        return false;
    }
    fprintf(stderr, "[IPC] connected to gatewayd\n");
    return true;
}

/* 处理写入失败：关闭连接并记录重连时间。调用者必须持有 g_mutex。 */
static void handle_write_error(void)
{
    close(g_fd);
    g_fd = -1;
    g_last_reconnect_ms = get_now_ms();
}

/* 使用 writev 发送帧（长度前缀 + 数据）。调用者必须持有 g_mutex。 */
static bool write_frame(const uint8_t *data, uint16_t len)
{
    uint8_t len_prefix[2];
    len_prefix[0] = (uint8_t)(len & 0xFF);
    len_prefix[1] = (uint8_t)((len >> 8) & 0xFF);

    struct iovec iov[2];
    iov[0].iov_base = len_prefix;
    iov[0].iov_len = 2;
    iov[1].iov_base = (void *)data;
    iov[1].iov_len = len;

    ssize_t total = 2 + len;
    ssize_t sent = 0;
    struct iovec *iov_ptr = iov;
    int iov_count = 2;

    while (sent < total) {
        ssize_t n = writev(g_fd, iov_ptr, iov_count);
        if (n <= 0) {
            return false;
        }
        sent += n;

        /* 调整 iov_ptr 以跳过已发送的数据 */
        ssize_t remaining = n;
        while (remaining > 0 && iov_count > 0) {
            if (remaining >= (ssize_t)iov_ptr[0].iov_len) {
                remaining -= iov_ptr[0].iov_len;
                iov_ptr++;
                iov_count--;
            } else {
                iov_ptr[0].iov_base = (uint8_t *)iov_ptr[0].iov_base + remaining;
                iov_ptr[0].iov_len -= remaining;
                remaining = 0;
            }
        }
    }
    return true;
}

bool ipc_sender_send_raw(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return false;
    }

    pthread_mutex_lock(&g_mutex);

    if (!ensure_connected()) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }

    bool ok = write_frame(data, len);
    if (!ok) {
        handle_write_error();
    }

    pthread_mutex_unlock(&g_mutex);
    return ok;
}

bool ipc_sender_send_batch(const ipc_frame_t *frames, int count)
{
    if (frames == NULL || count <= 0 || count > MAX_BATCH_FRAMES) {
        return false;
    }

    pthread_mutex_lock(&g_mutex);

    if (!ensure_connected()) {
        pthread_mutex_unlock(&g_mutex);
        return false;
    }

    /* 构建 iovec 数组，使用 writev 一次发送所有帧 */
    struct iovec iov[MAX_BATCH_FRAMES * 2];  /* 每帧需要 2 个 iov: len_prefix + data */
    int iov_count = 0;
    uint8_t len_prefixes[MAX_BATCH_FRAMES][2];

    for (int i = 0; i < count; i++) {
        len_prefixes[i][0] = (uint8_t)(frames[i].len & 0xFF);
        len_prefixes[i][1] = (uint8_t)((frames[i].len >> 8) & 0xFF);

        iov[iov_count].iov_base = len_prefixes[i];
        iov[iov_count].iov_len = 2;
        iov_count++;

        iov[iov_count].iov_base = (void *)frames[i].data;
        iov[iov_count].iov_len = frames[i].len;
        iov_count++;
    }

    /* 使用 writev 一次性发送所有帧 */
    ssize_t total_size = 0;
    for (int i = 0; i < iov_count; i++) {
        total_size += iov[i].iov_len;
    }

    struct iovec *iov_ptr = iov;
    int iov_remaining = iov_count;
    ssize_t total_sent = 0;
    bool ok = true;

    while (total_sent < total_size) {
        ssize_t n = writev(g_fd, iov_ptr, iov_remaining);
        if (n <= 0) {
            ok = false;
            break;
        }
        total_sent += n;

        /* 调整 iov_ptr 以跳过已发送的数据 */
        ssize_t remaining = n;
        while (remaining > 0 && iov_remaining > 0) {
            if (remaining >= (ssize_t)iov_ptr[0].iov_len) {
                remaining -= iov_ptr[0].iov_len;
                iov_ptr++;
                iov_remaining--;
            } else {
                iov_ptr[0].iov_base = (uint8_t *)iov_ptr[0].iov_base + remaining;
                iov_ptr[0].iov_len -= remaining;
                remaining = 0;
            }
        }
    }

    if (!ok) {
        handle_write_error();
    }

    pthread_mutex_unlock(&g_mutex);
    return ok;
}
