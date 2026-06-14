/*
 * ipc_cmd_receiver.c — IPC 命令接收器
 *
 * 职责：
 *   作为 Unix Socket 服务端监听 cmd_socket，接收 gatewayd 发送的命令帧。
 *   独立线程运行，接收命令后调用回调处理并发送响应帧。
 *
 * 线程模型：
 *   cmd_receiver 线程：监听 socket、读取命令帧、调用 handler、发送响应帧。
 *   main 线程：通过 ipc_cmd_receiver_deinit() 通知退出。
 *
 * 协议：
 *   2 字节 LE 长度前缀 + 帧体（与数据通道一致）。
 */

#include "ipc_cmd_receiver.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdatomic.h>
#include <poll.h>

/* ── 内部状态 ── */

#define CMD_SOCKET_BACKLOG  1
#define CMD_READ_TIMEOUT_MS 500   /* poll 超时，用于检查 running 标志 */

static int g_listen_fd = -1;
static int g_client_fd = -1;
static pthread_t g_thread;
static atomic_bool g_running = false;
static cmd_handler_fn g_handler = NULL;
static char g_socket_path[256];
static int g_socket_path_len = 0;

/* ── 内部函数 ── */

/*
 * 精确读取 n 字节。返回 true 表示读满。
 * 超时或错误返回 false。
 */
static bool read_exact(int fd, uint8_t *buf, size_t n)
{
    size_t received = 0;
    while (received < n) {
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int ret = poll(&pfd, 1, CMD_READ_TIMEOUT_MS);
        if (ret <= 0) {
            /* 超时或错误，检查是否需要退出 */
            if (!atomic_load(&g_running))
                return false;
            continue;
        }
        ssize_t bytes = read(fd, buf + received, n - received);
        if (bytes <= 0) {
            if (bytes < 0 && errno == EINTR)
                continue;
            return false;
        }
        received += (size_t)bytes;
    }
    return true;
}

/*
 * 读取一帧命令请求。返回帧体长度，0 表示失败。
 * 帧格式：2 字节 LE 长度前缀 + 帧体。
 */
static uint16_t read_cmd_frame(int fd, uint8_t *buf, uint16_t buf_size)
{
    uint8_t header[2];
    if (!read_exact(fd, header, 2))
        return 0;

    uint16_t frame_len = (uint16_t)header[0] | ((uint16_t)header[1] << 8);
    if (frame_len == 0 || frame_len > buf_size) {
        fprintf(stderr, "[CMD][WARN] invalid frame len=%u\n", frame_len);
        return 0;
    }

    if (!read_exact(fd, buf, frame_len))
        return 0;

    return frame_len;
}

/*
 * 发送一帧响应。返回 true 表示成功。
 */
static bool write_cmd_frame(int fd, const uint8_t *data, uint16_t len)
{
    uint8_t header[2] = {
        (uint8_t)(len & 0xFF),
        (uint8_t)((len >> 8) & 0xFF)
    };

    if (write(fd, header, 2) != 2)
        return false;

    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, data + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

/*
 * 处理一个客户端连接。
 * 循环读取命令帧，调用 handler 处理，发送响应帧。
 * 客户端断开或 running=false 时退出。
 */
static void handle_client(int client_fd)
{
    uint8_t frame_buf[7 + IPC_CMD_MAX_PARAM_LEN];
    uint8_t resp_buf[6 + IPC_CMD_MAX_DATA_LEN];

    fprintf(stderr, "[CMD][STATUS] gatewayd connected\n");

    while (atomic_load(&g_running)) {
        /* 读取命令帧 */
        uint16_t frame_len = read_cmd_frame(client_fd, frame_buf, sizeof(frame_buf));
        if (frame_len == 0) {
            fprintf(stderr, "[CMD][STATUS] gatewayd disconnected or read error\n");
            break;
        }

        /* 解析命令请求 */
        if (frame_len < 7) {
            fprintf(stderr, "[CMD][WARN] frame too short: %u\n", frame_len);
            continue;
        }

        ipc_cmd_request_t req;
        req.frame_type = frame_buf[0];
        req.seq = (uint16_t)frame_buf[1] | ((uint16_t)frame_buf[2] << 8);
        req.dtu_id = frame_buf[3];
        req.method = frame_buf[4];
        req.param_len = (uint16_t)frame_buf[5] | ((uint16_t)frame_buf[6] << 8);

        if (req.frame_type != IPC_FRAME_TYPE_CMD_REQUEST) {
            fprintf(stderr, "[CMD][WARN] unexpected frame type: 0x%02x\n", req.frame_type);
            continue;
        }

        if (req.param_len > IPC_CMD_MAX_PARAM_LEN) {
            fprintf(stderr, "[CMD][WARN] param too long: %u\n", req.param_len);
            continue;
        }

        if (req.param_len > 0 && frame_len >= 7 + req.param_len) {
            memcpy(req.param_data, frame_buf + 7, req.param_len);
        }

        fprintf(stderr, "[CMD][RX] dtu_id=%u method=%u seq=%u param_len=%u\n",
                req.dtu_id, req.method, req.seq, req.param_len);

        /* 调用命令处理器 */
        uint16_t resp_data_len = IPC_CMD_MAX_DATA_LEN;
        uint8_t result_code = CMD_RESULT_UNSUPPORTED;

        if (g_handler) {
            result_code = g_handler(&req, resp_buf + 6, &resp_data_len);
        } else {
            resp_data_len = 0;
        }

        /* 构建响应帧 */
        resp_buf[0] = IPC_FRAME_TYPE_CMD_RESPONSE;
        resp_buf[1] = (uint8_t)(req.seq & 0xFF);
        resp_buf[2] = (uint8_t)((req.seq >> 8) & 0xFF);
        resp_buf[3] = result_code;
        resp_buf[4] = (uint8_t)(resp_data_len & 0xFF);
        resp_buf[5] = (uint8_t)((resp_data_len >> 8) & 0xFF);

        const uint16_t resp_frame_len = (uint16_t)(6 + resp_data_len);

        if (!write_cmd_frame(client_fd, resp_buf, resp_frame_len)) {
            fprintf(stderr, "[CMD][WARN] write response failed\n");
            break;
        }

        fprintf(stderr, "[CMD][TX] seq=%u result=%u\n", req.seq, result_code);
    }
}

/*
 * 命令接收器线程入口。
 * 循环 accept 客户端连接，处理命令。
 */
static void *cmd_receiver_thread(void *arg)
{
    (void)arg;

    fprintf(stderr, "[CMD][STATUS] cmd receiver thread started\n");

    while (atomic_load(&g_running)) {
        /* accept 客户端连接（带超时） */
        struct pollfd pfd = {.fd = g_listen_fd, .events = POLLIN};
        int ret = poll(&pfd, 1, CMD_READ_TIMEOUT_MS);
        if (ret <= 0) {
            if (!atomic_load(&g_running))
                break;
            continue;
        }

        int client_fd = accept(g_listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[CMD][WARN] accept failed: %s\n", strerror(errno));
            continue;
        }

        g_client_fd = client_fd;
        handle_client(client_fd);
        close(client_fd);
        g_client_fd = -1;
    }

    fprintf(stderr, "[CMD][STATUS] cmd receiver thread stopped\n");
    return NULL;
}

/* ── 公开接口 ── */

int ipc_cmd_receiver_init(const char *socket_path, int path_len, cmd_handler_fn handler)
{
    if (!socket_path || path_len < 2 || !handler) {
        fprintf(stderr, "[CMD][ERROR] invalid params\n");
        return -1;
    }

    g_handler = handler;
    if (path_len < (int)sizeof(g_socket_path))
        memcpy(g_socket_path, socket_path, path_len);
    g_socket_path_len = path_len;

    /* 创建 Unix Socket（抽象命名空间） */
    g_listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_listen_fd < 0) {
        fprintf(stderr, "[CMD][ERROR] socket create failed: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    /* 抽象命名空间：首字节为 \0，后面是路径 */
    memcpy(addr.sun_path, socket_path, path_len);

    socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len);

    /* 尝试绑定，如果地址已被占用则尝试连接清理残留 socket */
    if (bind(g_listen_fd, (struct sockaddr *)&addr, addr_len) < 0) {
        if (errno == EADDRINUSE) {
            /* 抽象命名空间不需要 unlink，内核自动管理 */
            fprintf(stderr, "[CMD][WARN] socket in use, retrying\n");
            if (bind(g_listen_fd, (struct sockaddr *)&addr, addr_len) < 0) {
                fprintf(stderr, "[CMD][ERROR] bind failed after cleanup: %s\n", strerror(errno));
                close(g_listen_fd);
                g_listen_fd = -1;
                return -1;
            }
        } else {
            fprintf(stderr, "[CMD][ERROR] bind failed: %s\n", strerror(errno));
            close(g_listen_fd);
            g_listen_fd = -1;
            return -1;
        }
    }

    if (listen(g_listen_fd, CMD_SOCKET_BACKLOG) < 0) {
        fprintf(stderr, "[CMD][ERROR] listen failed: %s\n", strerror(errno));
        close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }

    atomic_store(&g_running, true);

    if (pthread_create(&g_thread, NULL, cmd_receiver_thread, NULL) != 0) {
        fprintf(stderr, "[CMD][ERROR] thread create failed\n");
        close(g_listen_fd);
        g_listen_fd = -1;
        atomic_store(&g_running, false);
        return -1;
    }

    fprintf(stderr, "[CMD][STATUS] cmd receiver initialized socket (len=%d)\n", path_len);
    return 0;
}

void ipc_cmd_receiver_deinit(void)
{
    atomic_store(&g_running, false);

    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }

    if (g_client_fd >= 0) {
        close(g_client_fd);
        g_client_fd = -1;
    }

    pthread_join(g_thread, NULL);
    g_handler = NULL;

    fprintf(stderr, "[CMD][STATUS] cmd receiver deinitialized\n");
}

bool ipc_cmd_receiver_is_running(void)
{
    return atomic_load(&g_running);
}
