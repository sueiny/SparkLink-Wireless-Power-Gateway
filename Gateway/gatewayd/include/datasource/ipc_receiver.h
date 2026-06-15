#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 从 Unix Socket 接收 sle-daemon 发送的原始 SLE 帧。
// 协议：每帧以 2 字节 LE 长度前缀 + 原始帧数据 传输。
namespace gateway::datasource {

enum class IpcReceiveStatus {
    Frame,
    Timeout,
    Disconnected,
    Error,
};

class IpcReceiver {
public:
    IpcReceiver();
    ~IpcReceiver();

    bool init(const std::string &socket_path);
    void deinit();

    // accept 一个客户端连接（阻塞，带超时）。
    bool acceptClient(int timeout_ms);

    // 读取一帧原始 SLE 数据（2 字节长度前缀 + 帧体）。
    // Timeout 仅表示当前无数据，不代表 IPC client 断开。
    IpcReceiveStatus receiveRawFrame(std::vector<uint8_t> &out);

    void closeClient();
    bool isConnected() const { return client_fd_ >= 0; }

private:
    enum class ReadStatus {
        Ok,
        Timeout,
        Disconnected,
        Error,
    };

    // 读取精确 n 字节；Timeout 表示当前帧尚未到达，不关闭 client。
    ReadStatus readExact(uint8_t *buf, size_t n);

    int listen_fd_;
    int client_fd_;
    std::string socket_path_;
};

} // namespace gateway::datasource
