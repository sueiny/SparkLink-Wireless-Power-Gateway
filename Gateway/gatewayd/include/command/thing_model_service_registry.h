#pragma once

#include "command/command_types.h"
#include "common/logger.h"
#include "common/device_model.h"

#include <map>
#include <string>
#include <vector>

namespace gateway::command {

// ThingModelServiceRegistry 从本地 things_model JSON 读取服务定义。
// 命令校验只依赖该表，避免代码中的服务白名单和平台物模型漂移。
class ThingModelServiceRegistry {
public:
    // config_path 用来推导 things_model 目录候选位置；成功后会记录实际加载目录。
    bool load(const std::string &config_path, log::Logger &logger);

    // 按设备类型和 method 查找服务定义；返回 nullptr 表示物模型未声明该服务。
    const ServiceSpec *findService(model::DeviceType type, const std::string &method) const;

private:
    // 尝试从指定目录加载所有已知设备类型的物模型文件。
    bool loadFromDir(const std::string &dir, log::Logger &logger);

    // 解析单个物模型文件中的 services/inputData。
    bool loadModelFile(model::DeviceType type, const std::string &path, log::Logger &logger);
    std::vector<std::string> candidateDirs(const std::string &config_path) const;

    std::map<model::DeviceType, std::map<std::string, ServiceSpec>> services_;
};

} // namespace gateway::command
