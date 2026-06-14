#pragma once

#include "common/logger.h"

#include <cstdint>
#include <string>

namespace gateway::datasource {

// IpcCmdSender 通过 Unix Socket 向 sle_data_app 发送命令并等待响应。
// 作为客户端连接到 sle_data_app 监听的 cmd_socket。
// 协议：2 字节 LE 长度前缀 + 帧体（与数据通道一致）。
class IpcCmdSender {
public:
    explicit IpcCmdSender(log::Logger &logger);
    ~IpcCmdSender();

    bool init(const std::string &socket_path);
    void deinit();

    // 发送命令并同步等待响应。
    // dtu_id: 目标 DTU ID (1-255)
    // method: CMD_METHOD_*
    // param_data/param_len: 参数数据
    // result_code: 输出响应码 (CMD_RESULT_*)
    // resp_data/resp_data_len: 输出响应数据
    // timeout_ms: 超时毫秒数
    // 返回 true 表示收到响应，false 表示通信失败或超时。
    bool sendCommand(uint8_t dtu_id, uint8_t method,
                     const uint8_t *param_data, uint16_t param_len,
                     uint8_t *result_code,
                     uint8_t *resp_data, uint16_t *resp_data_len,
                     int timeout_ms = 3000,
                     const std::string &request_id = {});

private:
    bool ensureConnected();
    bool writeFrame(const uint8_t *data, uint16_t len);
    bool readFrame(uint8_t *buf, uint16_t buf_size, uint16_t *out_len, int timeout_ms);

    int fd_ = -1;
    std::string socket_path_;
    uint16_t next_seq_ = 0;
    log::Logger &logger_;
};

} // namespace gateway::datasource
