#include "command/thing_model_service_registry.h"

#include "common/constants.h"
#include "common/file_utils.h"
#include "json.hpp"

#include <sstream>
#include <unistd.h>

namespace gateway::command {
namespace {

std::string dataTypeText(const nlohmann::json &item)
{
    const auto data_type = item.value("dataType", nlohmann::json::object());
    if (data_type.is_object())
        return data_type.value("type", "");
    if (data_type.is_string())
        return data_type.get<std::string>();
    return {};
}

std::vector<int64_t> enumValues(const nlohmann::json &item)
{
    std::vector<int64_t> values;
    const auto data_type = item.value("dataType", nlohmann::json::object());
    if (!data_type.is_object())
        return values;

    for (const auto &spec : data_type.value("specsList", nlohmann::json::array())) {
        if (spec.contains("value") && spec["value"].is_number_integer())
            values.push_back(spec["value"].get<int64_t>());
    }

    return values;
}

const std::map<model::DeviceType, std::string> &modelFiles()
{
    static const std::map<model::DeviceType, std::string> files = {
        {model::DeviceType::Gateway, "gateway_model.json"},
        {model::DeviceType::SinglePhaseMeter, "single_phase_meter_model.json"},
        {model::DeviceType::EnvSensor, "env_sensor_model.json"},
        {model::DeviceType::Relay, "relay_device_model.json"},
        {model::DeviceType::DtuNode, "dtu_node_model.json"},
    };
    return files;
}

} // namespace

bool ThingModelServiceRegistry::load(const std::string &config_path, log::Logger &logger)
{
    services_.clear();

    std::vector<std::string> tried_dirs;
    for (const auto &dir : candidateDirs(config_path)) {
        tried_dirs.push_back(dir);
        services_.clear();
        if (loadFromDir(dir, logger)) {
            logger.info("CMD", "loaded thing model services from " + dir);
            return true;
        }
    }

    std::ostringstream ss;
    ss << "failed to load thing model service definitions, tried=";
    for (size_t i = 0; i < tried_dirs.size(); ++i) {
        if (i > 0)
            ss << ",";
        ss << tried_dirs[i];
    }
    logger.error("CMD", ss.str());
    return false;
}

const ServiceSpec *ThingModelServiceRegistry::findService(model::DeviceType type,
                                                          const std::string &method) const
{
    const auto type_it = services_.find(type);
    if (type_it == services_.end())
        return nullptr;

    const auto service_it = type_it->second.find(method);
    if (service_it == type_it->second.end())
        return nullptr;

    return &service_it->second;
}

bool ThingModelServiceRegistry::loadFromDir(const std::string &dir, log::Logger &logger)
{
    for (const auto &[type, filename] : modelFiles()) {
        const std::string path = common::joinPath(dir, filename);
        if (!common::regularFileExists(path))
            return false;

        if (!loadModelFile(type, path, logger))
            return false;
    }

    return true;
}

bool ThingModelServiceRegistry::loadModelFile(model::DeviceType type,
                                              const std::string &path,
                                              log::Logger &logger)
{
    std::string text;
    common::readText(path, &text);
    if (text.empty()) {
        logger.warn("CMD", "empty or unreadable thing model file: " + path);
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const std::exception &e) {
        logger.warn("CMD", std::string("failed to parse thing model file: ") + path +
                               ", error=" + e.what());
        return false;
    }

    auto &service_map = services_[type];
    for (const auto &service : root.value("services", nlohmann::json::array())) {
        ServiceSpec spec;
        spec.identifier = service.value("identifier", "");
        spec.call_type = service.value("callType", "ASYNC");
        if (spec.identifier.empty())
            continue;

        for (const auto &param : service.value("inputData", nlohmann::json::array())) {
            ServiceParamSpec param_spec;
            param_spec.identifier = param.value("identifier", "");
            param_spec.type = dataTypeText(param);
            param_spec.enum_values = enumValues(param);
            if (!param_spec.identifier.empty())
                spec.input_params.push_back(std::move(param_spec));
        }

        service_map[spec.identifier] = std::move(spec);
    }

    return true;
}

std::vector<std::string> ThingModelServiceRegistry::candidateDirs(const std::string &config_path) const
{
    std::vector<std::string> dirs;
    const std::string config_dir = common::dirnameOf(config_path);
    dirs.push_back(common::joinPath(config_dir, "../things_model"));
    dirs.push_back(common::joinPath(config_dir, "../gatewayd/things_model"));

    char cwd[512] = {0};
    if (::getcwd(cwd, sizeof(cwd))) {
        dirs.push_back(common::joinPath(cwd, "things_model"));
        dirs.push_back(common::joinPath(cwd, "../things_model"));
        dirs.push_back(common::joinPath(cwd, "app/Gateway/gatewayd/things_model"));
    }

    dirs.push_back(common::kDefaultThingsModelDir);
    dirs.push_back(common::joinPath(common::kGatewayBasePath, "gatewayd/things_model"));
    return dirs;
}

} // namespace gateway::command
