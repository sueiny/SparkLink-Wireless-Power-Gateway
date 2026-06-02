#include "datasource/ipc_receiver.h"

#include <cerrno>
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
    mkdir("/var/run", 0755);
    mkdir("/var/run/gateway", 0755);
    unlink(socket_path_.c_str());

    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        fprintf(stderr, "[IPC] socket create failed: %s\n", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());

    if (bind(listen_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[IPC] bind failed: %s\n", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 1) < 0) {
        fprintf(stderr, "[IPC] listen failed: %s\n", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    fprintf(stderr, "[IPC] receiver listening on %s\n", socket_path_.c_str());
    return true;
}

void IpcReceiver::deinit()
{
    closeClient();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }
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

bool IpcReceiver::readExact(uint8_t *buf, size_t n)
{
    size_t received = 0;
    while (received < n) {
        // 先 poll 等待数据到达，避免阻塞在 read() 导致无法响应 stop()
        struct pollfd pfd = {client_fd_, POLLIN, 0};
        int pr = poll(&pfd, 1, 200);  // 200ms 超时
        if (pr <= 0) return false;     // 超时或错误

        ssize_t r = read(client_fd_, buf + received, n - received);
        if (r <= 0) return false;
        received += static_cast<size_t>(r);
    }
    return true;
}

bool IpcReceiver::receiveRawFrame(std::vector<uint8_t> &out)
{
    if (client_fd_ < 0) return false;

    // 读 2 字节 LE 长度前缀
    uint8_t len_buf[2];
    if (!readExact(len_buf, 2)) {
        return false;
    }
    uint16_t frame_len = static_cast<uint16_t>(len_buf[0] | (len_buf[1] << 8));

    if (frame_len == 0 || frame_len > 256) {
        fprintf(stderr, "[IPC] invalid frame length: %u\n", frame_len);
        return false;
    }

    // 读帧体
    out.resize(frame_len);
    if (!readExact(out.data(), frame_len)) {
        return false;
    }

    return true;
}

void IpcReceiver::closeClient()
{
    if (client_fd_ >= 0) {
        close(client_fd_);
        client_fd_ = -1;
    }
}

} // namespace gateway::datasource
