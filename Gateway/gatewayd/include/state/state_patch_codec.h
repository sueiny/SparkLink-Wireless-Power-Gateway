#pragma once

#include "command/command_types.h"
#include "config/config_manager.h"
#include "common/device_model.h"

namespace gateway::codec {

// 把“命令执行成功”转换成物模型属性补丁。
// 例如 set_relay.state -> relay_state/relay_status。
class StatePatchCodec {
public:
    // 返回 false 表示该命令当前没有对应的状态变化，不需要即时遥测。
    bool buildPatch(const command::CommandRequest &request,
                    const command::CommandResult &result,
                    const config::AppConfig &config,
                    model::TelemetryData *out) const;
};

} // namespace gateway::codec
