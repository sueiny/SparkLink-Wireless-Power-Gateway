#include "datasource/ipc_cmd_sender.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

// ipc_cmd_protocol.h 定义在 sle_data_app/inc/，通过 include path 引用
#include "ipc_cmd_protocol.h"

namespace gateway::datasource {
namespace {

std::string requestLogField(const std::string &request_id)
{
    return request_id.empty() ? std::string() : "request_id=" + request_id + ", ";
}

} // namespace

IpcCmdSender::IpcCmdSender(log::Logger &logger)
    : logger_(logger)
{
}

IpcCmdSender::~IpcCmdSender()
{
    deinit();
}

bool IpcCmdSender::init(const std::string &socket_path)
{
    socket_path_ = socket_path;
    return true;
}

void IpcCmdSender::deinit()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool IpcCmdSender::ensureConnected()
{
    if (fd_ >= 0)
        return true;

    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        logger_.warn("CMD", std::string("cmd socket create failed: ") + std::strerror(errno));
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    // 抽象命名空间：首字节为 \0，后面跟完整路径（含开头的 /）
    addr.sun_path[0] = '\0';
    std::strncpy(addr.sun_path + 1, socket_path_.c_str(), sizeof(addr.sun_path) - 2);

    const socklen_t addr_len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + socket_path_.size());

    if (::connect(fd_, reinterpret_cast<struct sockaddr *>(&addr), addr_len) < 0) {
        ::close(fd_);
        fd_ = -1;
        // 连接失败不打日志（sle_data_app 可能未启动），调用方静默处理
        return false;
    }

    logger_.info("CMD", "cmd socket connected to sle_data_app");
    return true;
}

bool IpcCmdSender::writeFrame(const uint8_t *data, uint16_t len)
{
    // 2 字节 LE 长度前缀 + 帧体
    uint8_t header[2] = {
        static_cast<uint8_t>(len & 0xFF),
        static_cast<uint8_t>((len >> 8) & 0xFF)
    };

    // 先发长度前缀
    ssize_t n = ::write(fd_, header, 2);
    if (n != 2) {
        logger_.warn("CMD", "cmd frame header write failed");
        return false;
    }

    // 再发帧体
    size_t sent = 0;
    while (sent < len) {
        n = ::write(fd_, data + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            logger_.warn("CMD", std::string("cmd frame body write failed: ") + std::strerror(errno));
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool IpcCmdSender::readFrame(uint8_t *buf, uint16_t buf_size, uint16_t *out_len, int timeout_ms)
{
    // 等待可读
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret <= 0)
        return false;

    // 读 2 字节长度前缀
    uint8_t header[2];
    ssize_t n = ::read(fd_, header, 2);
    if (n != 2)
        return false;

    uint16_t frame_len = static_cast<uint16_t>(header[0]) | (static_cast<uint16_t>(header[1]) << 8);
    if (frame_len == 0 || frame_len > buf_size)
        return false;

    // 读帧体
    size_t received = 0;
    while (received < frame_len) {
        n = ::read(fd_, buf + received, frame_len - received);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        received += static_cast<size_t>(n);
    }

    *out_len = frame_len;
    return true;
}

bool IpcCmdSender::sendCommand(uint8_t dtu_id, uint8_t method,
                               const uint8_t *param_data, uint16_t param_len,
                               uint8_t *result_code,
                               uint8_t *resp_data, uint16_t *resp_data_len,
                               int timeout_ms,
                               const std::string &request_id)
{
    const std::string request_field = requestLogField(request_id);
    if (param_len > IPC_CMD_MAX_PARAM_LEN) {
        logger_.warn("CMD", "cmd rejected " + request_field +
                            "reason=param_too_large, param_len=" + std::to_string(param_len) +
                            ", max=" + std::to_string(IPC_CMD_MAX_PARAM_LEN));
        return false;
    }

    if (!ensureConnected())
        return false;

    // 构建命令请求帧
    ipc_cmd_request_t req{};
    req.frame_type = IPC_FRAME_TYPE_CMD_REQUEST;
    req.seq = next_seq_++;
    req.dtu_id = dtu_id;
    req.method = method;
    req.param_len = param_len;
    if (param_len > 0 && param_data) {
        std::memcpy(req.param_data, param_data, param_len);
    }

    // 帧体 = 结构体固定部分 + 参数数据
    const uint16_t frame_len = static_cast<uint16_t>(7 + param_len);
    uint8_t frame_buf[7 + IPC_CMD_MAX_PARAM_LEN];
    frame_buf[0] = req.frame_type;
    frame_buf[1] = static_cast<uint8_t>(req.seq & 0xFF);
    frame_buf[2] = static_cast<uint8_t>((req.seq >> 8) & 0xFF);
    frame_buf[3] = req.dtu_id;
    frame_buf[4] = req.method;
    frame_buf[5] = static_cast<uint8_t>(param_len & 0xFF);
    frame_buf[6] = static_cast<uint8_t>((param_len >> 8) & 0xFF);
    if (param_len > 0 && param_data) {
        std::memcpy(frame_buf + 7, param_data, param_len);
    }

    if (!writeFrame(frame_buf, frame_len)) {
        deinit();
        return false;
    }

    logger_.info("CMD", "cmd sent " + request_field +
                         "dtu_id=" + std::to_string(dtu_id) +
                         ", method=" + std::to_string(method) +
                         ", seq=" + std::to_string(req.seq));

    // 读取响应帧
    uint8_t read_buf[2 + sizeof(ipc_cmd_response_t)];
    uint16_t read_len = 0;
    if (!readFrame(read_buf, sizeof(read_buf), &read_len, timeout_ms)) {
        logger_.warn("CMD", "cmd response timeout or read failed " + request_field +
                            "seq=" + std::to_string(req.seq));
        deinit();
        return false;
    }

    // 解析响应帧
    if (read_len < 6) {
        logger_.warn("CMD", "cmd response too short " + request_field +
                            "len=" + std::to_string(read_len));
        return false;
    }

    // 跳过 2 字节长度前缀
    const uint8_t *resp = read_buf;
    uint8_t resp_frame_type = resp[0];
    uint16_t resp_seq = static_cast<uint16_t>(resp[1]) | (static_cast<uint16_t>(resp[2]) << 8);

    if (resp_frame_type != IPC_FRAME_TYPE_CMD_RESPONSE) {
        logger_.warn("CMD", "cmd response invalid frame type " + request_field +
                            "frame_type=" + std::to_string(resp_frame_type));
        return false;
    }

    if (resp_seq != req.seq) {
        logger_.warn("CMD", "cmd response seq mismatch " + request_field + "expected=" +
                             std::to_string(req.seq) + ", got=" + std::to_string(resp_seq));
        return false;
    }

    *result_code = resp[3];
    uint16_t data_len = static_cast<uint16_t>(resp[4]) | (static_cast<uint16_t>(resp[5]) << 8);
    if (data_len > *resp_data_len || data_len > IPC_CMD_MAX_DATA_LEN) {
        logger_.warn("CMD", "cmd response data too long " + request_field +
                            "data_len=" + std::to_string(data_len));
        return false;
    }

    if (data_len > 0 && read_len >= 6 + data_len) {
        std::memcpy(resp_data, resp + 6, data_len);
    }
    *resp_data_len = data_len;

    logger_.info("CMD", "cmd response received " + request_field +
                         "seq=" + std::to_string(resp_seq) +
                         ", result=" + std::to_string(*result_code));
    return true;
}

} // namespace gateway::datasource
