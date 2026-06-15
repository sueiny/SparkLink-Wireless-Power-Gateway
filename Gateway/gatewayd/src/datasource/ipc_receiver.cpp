#include "datasource/ipc_receiver.h"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

namespace gateway::datasource {

IpcReceiver::IpcReceiver() : listen_fd_(-1), client_fd_(-1) {}


IpcReceiver::~IpcReceiver()
{
    deinit();
}

bool IpcReceiver::init(const std::string &socket_path)
{
    socket_path_ = socket_path;

    // 尝试绑定，如果失败则尝试连接以清理旧 socket
    for (int attempt = 0; attempt < 2; attempt++) {
        // 使用 SOCK_CLOEXEC 确保子进程不会继承此 fd
        listen_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (listen_fd_ < 0) {
            fprintf(stderr, "[IPC] socket create failed: %s\n", strerror(errno));
            return false;
        }

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        // 使用抽象命名空间（以 \0 开头），不需要文件系统支持
        // 去掉路径开头的 '/'，前面加 \0
        std::string abstract_name = socket_path;
        if (!abstract_name.empty() && abstract_name[0] == '/')
            abstract_name = abstract_name.substr(1);
        addr.sun_path[0] = '\0';
        snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, "%s", abstract_name.c_str());

        socklen_t addrlen = offsetof(struct sockaddr_un, sun_path) + 1 + abstract_name.size();
        if (bind(listen_fd_, (struct sockaddr *)&addr, addrlen) == 0) {
            // 绑定成功
            break;
        }

        if (errno != EADDRINUSE || attempt == 1) {
            fprintf(stderr, "[IPC] bind failed: %s\n", strerror(errno));
            close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        // 第一次绑定失败，尝试连接以清理旧 socket
        fprintf(stderr, "[IPC] socket in use, attempting cleanup...\n");
        close(listen_fd_);
        listen_fd_ = -1;

        int probe_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe_fd >= 0) {
            if (connect(probe_fd, (struct sockaddr *)&addr, addrlen) == 0) {
                // 连接成功，说明有活跃的服务器，关闭连接
                fprintf(stderr, "[IPC] active server detected, waiting...\n");
                close(probe_fd);
                sleep(2);
            } else {
                // 连接失败，socket 是残留的，可以覆盖
                close(probe_fd);
                fprintf(stderr, "[IPC] stale socket detected, retrying bind...\n");
            }
        }
    }

    if (listen(listen_fd_, 1) < 0) {
        fprintf(stderr, "[IPC] listen failed: %s\n", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    std::string abstract_name = socket_path;
    if (!abstract_name.empty() && abstract_name[0] == '/')
        abstract_name = abstract_name.substr(1);
    fprintf(stderr, "[IPC] receiver listening on @%s (abstract)\n", abstract_name.c_str());
    return true;
}

void IpcReceiver::deinit()
{
    closeClient();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    // 抽象命名空间 socket 不需要 unlink
}

bool IpcReceiver::acceptClient(int timeout_ms)
{
    if (listen_fd_ < 0) return false;

    struct pollfd pfd;
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, timeout_ms) <= 0) return false;

    int fd = accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) return false;

    closeClient();
    client_fd_ = fd;
    fprintf(stderr, "[IPC] client connected\n");
    return true;
}

IpcReceiver::ReadStatus IpcReceiver::readExact(uint8_t *buf, size_t n)
{
    size_t received = 0;
    while (received < n) {
        // 先 poll 等待数据到达；超时只表示当前没有完整帧，不能当成断连。
        struct pollfd pfd = {client_fd_, POLLIN, 0};
        int pr = poll(&pfd, 1, 200);  // 200ms 超时
        if (pr == 0 && received == 0)
            return ReadStatus::Timeout;
        if (pr == 0)
            continue;
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            return ReadStatus::Error;
        }

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return ReadStatus::Disconnected;
        if (!(pfd.revents & POLLIN))
            continue;

        ssize_t r = read(client_fd_, buf + received, n - received);
        if (r == 0)
            return ReadStatus::Disconnected;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return ReadStatus::Error;
        }
        received += static_cast<size_t>(r);
    }
    return ReadStatus::Ok;
}

IpcReceiveStatus IpcReceiver::receiveRawFrame(std::vector<uint8_t> &out)
{
    if (client_fd_ < 0) return IpcReceiveStatus::Disconnected;

    // 读 2 字节 LE 长度前缀
    uint8_t len_buf[2];
    ReadStatus status = readExact(len_buf, 2);
    if (status != ReadStatus::Ok) {
        if (status == ReadStatus::Timeout)
            return IpcReceiveStatus::Timeout;
        if (status == ReadStatus::Error)
            return IpcReceiveStatus::Error;
        return IpcReceiveStatus::Disconnected;
    }
    uint16_t frame_len = static_cast<uint16_t>(len_buf[0] | (len_buf[1] << 8));

    if (frame_len == 0 || frame_len > 256) {
        fprintf(stderr, "[IPC] invalid frame length: %u\n", frame_len);
        return IpcReceiveStatus::Error;
    }

    // 读帧体
    out.resize(frame_len);
    status = readExact(out.data(), frame_len);
    if (status != ReadStatus::Ok) {
        out.clear();
        if (status == ReadStatus::Timeout)
            return IpcReceiveStatus::Error;
        if (status == ReadStatus::Error)
            return IpcReceiveStatus::Error;
        return IpcReceiveStatus::Disconnected;
    }

    return IpcReceiveStatus::Frame;
}

void IpcReceiver::closeClient()
{
    if (client_fd_ >= 0) {
        close(client_fd_);
        client_fd_ = -1;
    }
}

} // namespace gateway::datasource
