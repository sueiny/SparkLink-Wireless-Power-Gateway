#include "command/command_executor.h"
#include "datasource/ipc_cmd_sender.h"

#include "codec/sle_downlink_builder.h"
#include "ipc_cmd_protocol.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace gateway::command {
namespace {

int jsonIntOr(const nlohmann::json &params, const char *key, int fallback)
{
    if (params.contains(key) && params[key].is_number_integer())
        return params[key].get<int>();
    return fallback;
}

const model::DeviceInfo *findDevice(const CommandRequest &request,
                                    const config::AppConfig &config)
{
    const auto it = std::find_if(config.devices.begin(), config.devices.end(),
                                 [&](const model::DeviceInfo &device) {
                                     return device.device_id == request.target_device_id;
                                 });
    return it == config.devices.end() ? nullptr : &*it;
}

const model::DtuDeviceInfo *findDtu(int node_id, const config::AppConfig &config)
{
    const auto it = std::find_if(config.dtu_devices.begin(), config.dtu_devices.end(),
                                 [&](const model::DtuDeviceInfo &dtu) {
                                     return dtu.node_id == node_id;
                                 });
    return it == config.dtu_devices.end() ? nullptr : &*it;
}

uint16_t rootIdForDtu(int dtu_id, const config::AppConfig &config)
{
    int current = dtu_id;
    for (size_t depth = 0; depth < config.dtu_devices.size() + 1; ++depth) {
        const auto *dtu = findDtu(current, config);
        if (!dtu)
            return static_cast<uint16_t>(dtu_id);
        if (dtu->parent_id <= 0)
            return static_cast<uint16_t>(dtu->node_id);
        current = dtu->parent_id;
    }
    return static_cast<uint16_t>(dtu_id);
}

// 将 method 字符串转为 CMD_METHOD_* 枚举
uint8_t methodToCmdType(const std::string &method)
{
    if (method == "set_relay")         return CMD_METHOD_SET_RELAY;
    if (method == "set_mode")          return CMD_METHOD_SET_MODE;
    if (method == "set_collect_cycle") return CMD_METHOD_SET_COLLECT_CYCLE;
    if (method == "trigger_collect")   return CMD_METHOD_TRIGGER_COLLECT;
    if (method == "reboot")            return CMD_METHOD_REBOOT;
    return 0;
}

void writeU16LE(uint8_t *out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

constexpr int kDefaultIpcTimeoutMs = 3000;
constexpr int kRawStIpcTimeoutMs = 6000;

} // namespace

CommandExecutor::CommandExecutor(datasource::IpcCmdSender *ipc_cmd_sender)
    : ipc_cmd_sender_(ipc_cmd_sender)
{
}

int CommandExecutor::findDtuIdForDevice(const CommandRequest &request,
                                        const config::AppConfig &config) const
{
    // DTU 节点自身
    if (request.target_type == model::DeviceType::DtuNode) {
        const auto it = std::find_if(config.dtu_devices.begin(), config.dtu_devices.end(),
                                     [&](const model::DtuDeviceInfo &dtu) {
                                         return dtu.device_id == request.target_device_id;
                                     });
        if (it != config.dtu_devices.end())
            return it->node_id;
        return -1;
    }

    // 外接设备（METER/RELAY/ENV）：通过 dtu_id 字段找到挂载的 DTU
    const auto it = std::find_if(config.devices.begin(), config.devices.end(),
                                 [&](const model::DeviceInfo &dev) {
                                     return dev.device_id == request.target_device_id;
                                 });
    if (it != config.devices.end() && it->dtu_id > 0)
        return it->dtu_id;

    return -1;
}

CommandResult CommandExecutor::executeViaIpc(const CommandRequest &request,
                                            uint8_t dtu_id,
                                            const config::AppConfig &config) const
{
    uint8_t cmd_type = methodToCmdType(request.method);
    if (cmd_type == 0) {
        return makeCommandResult(false, "UNSUPPORTED", "unknown method: " + request.method);
    }

    std::array<uint8_t, IPC_CMD_MAX_PARAM_LEN> param_storage{};
    const uint8_t *param_data = nullptr;
    uint16_t param_len = 0;
    nlohmann::json command_data = nlohmann::json::object();

    if (request.method == "set_relay") {
        const auto *device = findDevice(request, config);
        if (!device)
            return makeCommandResult(false, "UNKNOWN_DEVICE", "target device not found");

        const uint16_t root_id = rootIdForDtu(device->dtu_id, config);
        const uint8_t relay_channel =
            static_cast<uint8_t>(jsonIntOr(request.params, "relay_channel",
                                           jsonIntOr(request.params, "channel", 0)));
        codec::SleDownlinkFrame downlink;
        std::string error;
        if (!codec::buildSetRelayDownlinkFrame(
                *device,
                root_id,
                jsonIntOr(request.params, "state", 0),
                relay_channel,
                next_downlink_seq_++,
                &downlink,
                &error)) {
            return makeCommandResult(false, "BAD_PARAMS", error.empty() ? "failed to build ST frame" : error);
        }

        const uint16_t st_len = static_cast<uint16_t>(downlink.frame.size());
        const uint16_t raw_param_len =
            static_cast<uint16_t>(IPC_CMD_RAW_ST_META_LEN + st_len);
        if (st_len > IPC_CMD_MAX_ST_FRAME_LEN || raw_param_len > IPC_CMD_MAX_PARAM_LEN) {
            return makeCommandResult(false, "PARAM_TOO_LARGE", "raw ST downlink frame too large");
        }

        cmd_type = CMD_METHOD_RAW_ST_DOWNLINK;
        writeU16LE(param_storage.data(), downlink.root_node_id);
        writeU16LE(param_storage.data() + 2, downlink.dst_node_id);
        writeU16LE(param_storage.data() + 4, st_len);
        std::memcpy(param_storage.data() + IPC_CMD_RAW_ST_META_LEN,
                    downlink.frame.data(),
                    downlink.frame.size());
        param_data = param_storage.data();
        param_len = raw_param_len;
        command_data = {
            {"result", 1},
            {"root_id", downlink.root_node_id},
            {"dst_node_id", downlink.dst_node_id},
            {"target_dtu_id", downlink.target_node_id},
            {"st_len", st_len},
            {"st_hex", codec::hexText(downlink.frame.data(), downlink.frame.size())},
        };
    } else {
        // 非 raw ST 命令暂保留旧参数形态；sle_data_app 会明确返回 unsupported。
        const std::string params_str = request.params.dump();
        if (params_str.size() > IPC_CMD_MAX_PARAM_LEN) {
            return makeCommandResult(false, "PARAM_TOO_LARGE",
                                     "command params too large: " +
                                         std::to_string(params_str.size()) +
                                         " bytes, max " +
                                         std::to_string(IPC_CMD_MAX_PARAM_LEN));
        }
        std::memcpy(param_storage.data(), params_str.data(), params_str.size());
        param_data = param_storage.data();
        param_len = static_cast<uint16_t>(params_str.size());
    }

    uint8_t result_code = CMD_RESULT_FAILED;
    uint8_t resp_data[IPC_CMD_MAX_DATA_LEN];
    uint16_t resp_data_len = sizeof(resp_data);

    const int ipc_timeout_ms =
        cmd_type == CMD_METHOD_RAW_ST_DOWNLINK ? kRawStIpcTimeoutMs : kDefaultIpcTimeoutMs;

    const bool ok = ipc_cmd_sender_->sendCommand(
        dtu_id, cmd_type, param_data, param_len,
        &result_code, resp_data, &resp_data_len, ipc_timeout_ms, request.request_id);

    if (!ok) {
        return makeCommandResult(false, "IPC_FAILED", "failed to send command to sle_data_app");
    }

    // 解析响应
    std::string resp_message;
    nlohmann::json resp_json = nlohmann::json::object();
    if (resp_data_len > 0) {
        try {
            resp_json = nlohmann::json::parse(resp_data, resp_data + resp_data_len);
            if (resp_json.contains("message"))
                resp_message = resp_json["message"].get<std::string>();
        } catch (...) {
            resp_message = std::string(reinterpret_cast<char *>(resp_data), resp_data_len);
        }
    }

    switch (result_code) {
    case CMD_RESULT_OK:
        return makeCommandResult(true, "OK", resp_message.empty() ? "command executed" : resp_message,
                                 resp_json.contains("data") ? resp_json["data"] :
                                     (resp_json.empty() ? command_data : resp_json));
    case CMD_RESULT_TIMEOUT:
        return makeCommandResult(false, "TIMEOUT", "command execution timed out on device");
    case CMD_RESULT_UNSUPPORTED:
        return makeCommandResult(false, "UNSUPPORTED",
                                 resp_message.empty() ? "device does not support this command" : resp_message);
    default:
        return makeCommandResult(false, "FAILED",
                                 resp_message.empty() ? "command execution failed" : resp_message);
    }
}

CommandResult CommandExecutor::executeSimulated(const CommandRequest &request) const
{
    if (request.method == "set_relay") {
        return makeCommandResult(true, "OK", "relay command accepted (simulated)",
                                 {{"result", 1},
                                  {"state", jsonIntOr(request.params, "state", 0)}});
    }

    if (request.method == "set_mode") {
        return makeCommandResult(true, "OK", "mode command accepted (simulated)",
                                 {{"result", 1},
                                  {"mode", jsonIntOr(request.params, "mode", 0)}});
    }

    if (request.method == "set_collect_cycle") {
        return makeCommandResult(true, "OK", "collect cycle command accepted (simulated)",
                                 {{"result", 1},
                                  {"cycle_ms", jsonIntOr(request.params, "cycle_ms", 0)}});
    }

    if (request.method == "trigger_collect") {
        return makeCommandResult(true, "OK", "collect command accepted (simulated)", {{"result", 1}});
    }

    if (request.method == "reboot") {
        return makeCommandResult(true, "OK", "reboot command accepted (simulated)", {{"result", 1}});
    }

    if (request.method == "ota_upgrade") {
        return makeCommandResult(false, "UNSUPPORTED", "ota upgrade execution is reserved",
                                 {{"result", 0}});
    }

    if (request.method == "clear_energy") {
        return makeCommandResult(false, "UNSUPPORTED", "clear energy execution is reserved",
                                 {{"result", 0}});
    }

    return makeCommandResult(false, "UNSUPPORTED", "command execution is unsupported",
                             {{"result", 0}});
}

CommandResult CommandExecutor::execute(const CommandRequest &request,
                                       const config::AppConfig &config) const
{
    // Gateway 自身命令不走 IPC（reboot 等后续可扩展）
    if (request.target_type == model::DeviceType::Gateway) {
        return executeSimulated(request);
    }

    // 这轮只真实化 set_relay 下发，其它命令继续走模拟路径。
    if (ipc_cmd_sender_ && request.method == "set_relay") {
        const int dtu_id = findDtuIdForDevice(request, config);
        if (dtu_id > 0) {
            return executeViaIpc(request, static_cast<uint8_t>(dtu_id), config);
        }
    }

    return executeSimulated(request);
}

} // namespace gateway::command
