#pragma once

#include "command/command_types.h"
#include "command/thing_model_service_registry.h"

namespace gateway::command {

// CommandValidator 按物模型 services/inputData 校验命令合法性。
// 它不执行命令，只返回可直接转换为云端响应的校验结果。
class CommandValidator {
public:
    // 校验顺序：目标设备是否存在 -> 服务是否存在 -> 参数是否齐全 -> 参数类型/枚举值。
    CommandResult validate(const CommandRequest &request,
                           const ThingModelServiceRegistry &registry) const;

private:
    // 仅按当前物模型需要支持 INT/ENUM/TEXT；后续如增加 BOOL/DOUBLE 在这里扩展。
    bool paramTypeValid(const nlohmann::json &value, const ServiceParamSpec &spec) const;
};

} // namespace gateway::command
