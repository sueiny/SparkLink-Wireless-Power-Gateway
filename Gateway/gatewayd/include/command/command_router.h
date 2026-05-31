#pragma once

#include "command/command_types.h"
#include "config/config_manager.h"

#include <string>

namespace gateway::command {

// CommandRouter 只负责把 ThingsKit 下行 topic/payload 转成内部 CommandRequest。
// 它不判断服务是否存在，也不执行命令；这些分别交给 Validator 和 Executor。
class CommandRouter {
public:
    // 支持 ThingsKit RPC topic 和网关命令 topic。
    // BAD_JSON 会生成 immediate_response；未知 topic 返回空结果让上层忽略。
    CommandParseResult parse(const RawCommandMessage &raw,
                             const config::AppConfig &config) const;

    // 把内部执行结果包装成统一响应 JSON，并保留原 payload 中的 device/deviceName。
    CommandResponseMessage buildResponse(const CommandRequest &request,
                                         const CommandResult &result) const;

private:
    std::string responseTopicFor(const std::string &topic) const;
    std::string requestIdFromTopic(const std::string &topic) const;
};

} // namespace gateway::command
