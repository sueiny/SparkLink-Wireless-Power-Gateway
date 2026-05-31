#pragma once

#include "command/command_types.h"

namespace gateway::command {

// CommandExecutor 第一阶段只做模拟执行闭环。
// 后续接 DTU/SLE 控制帧时，可以保持 CommandRequest/CommandResult 这层接口不变。
class CommandExecutor {
public:
    // 返回 OK/UNSUPPORTED 等结构化结果。
    // 当前不访问硬件，因此 success 只代表“模拟执行接受”。
    CommandResult execute(const CommandRequest &request) const;
};

} // namespace gateway::command
