#include "command/command_executor.h"
#include "datasource/ipc_cmd_sender.h"

#include "ipc_cmd_protocol.h"

#include <algorithm>
#include <cstring>

namespace gateway::command {
namespace {

int jsonIntOr(const nlohmann::json &params, const char *key, int fallback)
{
    if (params.contains(key) && params[key].is_number_integer())
        return params[key].get<int>();
    return fallback;
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

CommandResult CommandExecutor::executeViaIpc(const CommandRequest &request, uint8_t dtu_id) const
{
    const uint8_t cmd_type = methodToCmdType(request.method);
    if (cmd_type == 0) {
        return makeCommandResult(false, "UNSUPPORTED", "unknown method: " + request.method);
    }

    // 序列化 params 为 JSON 字符串作为参数
    const std::string params_str = request.params.dump();
    if (params_str.size() > IPC_CMD_MAX_PARAM_LEN) {
        return makeCommandResult(false, "PARAM_TOO_LARGE",
                                 "command params too large: " +
                                     std::to_string(params_str.size()) +
                                     " bytes, max " +
                                     std::to_string(IPC_CMD_MAX_PARAM_LEN));
    }

    const auto *param_data = reinterpret_cast<const uint8_t *>(params_str.data());
    const uint16_t param_len = static_cast<uint16_t>(params_str.size());

    uint8_t result_code = CMD_RESULT_FAILED;
    uint8_t resp_data[IPC_CMD_MAX_DATA_LEN];
    uint16_t resp_data_len = sizeof(resp_data);

    const bool ok = ipc_cmd_sender_->sendCommand(
        dtu_id, cmd_type, param_data, param_len,
        &result_code, resp_data, &resp_data_len, 3000, request.request_id);

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
                                 resp_json.contains("data") ? resp_json["data"] : resp_json);
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

    // 有 IPC 发送器时，尝试通过 IPC 发送到 sle_data_app
    if (ipc_cmd_sender_) {
        const int dtu_id = findDtuIdForDevice(request, config);
        if (dtu_id > 0) {
            return executeViaIpc(request, static_cast<uint8_t>(dtu_id));
        }
        // 找不到 DTU，回退到模拟
    }

    return executeSimulated(request);
}

} // namespace gateway::command
