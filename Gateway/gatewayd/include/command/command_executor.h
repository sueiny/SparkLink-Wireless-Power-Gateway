#pragma once

#include "command/command_types.h"
#include "config/config_manager.h"

#include <cstdint>

namespace gateway::datasource {
class IpcCmdSender;
} // namespace gateway::datasource

namespace gateway::command {

// CommandExecutor 负责将命令请求转化为实际控制动作。
// 有 IpcCmdSender 时通过 IPC 发送到 sle_data_app 执行；
// 无 IpcCmdSender 时走模拟路径（兼容测试）。
class CommandExecutor {
public:
    // ipc_cmd_sender: 可选，nullptr 时走模拟路径。
    explicit CommandExecutor(datasource::IpcCmdSender *ipc_cmd_sender = nullptr);

    // 返回 OK/UNSUPPORTED 等结构化结果。
    CommandResult execute(const CommandRequest &request, const config::AppConfig &config) const;

private:
    // 通过 IPC 发送命令到 sle_data_app
    CommandResult executeViaIpc(const CommandRequest &request, uint8_t dtu_id) const;

    // 模拟执行（兼容无 IPC 场景）
    CommandResult executeSimulated(const CommandRequest &request) const;

    // 从 config 中查找目标设备对应的 DTU ID
    int findDtuIdForDevice(const CommandRequest &request, const config::AppConfig &config) const;

    datasource::IpcCmdSender *ipc_cmd_sender_;
};

} // namespace gateway::command
