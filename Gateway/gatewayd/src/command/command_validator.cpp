#include "command/command_validator.h"

#include <algorithm>
#include <cctype>

namespace gateway::command {
namespace {

std::string upperText(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

} // namespace

CommandResult CommandValidator::validate(const CommandRequest &request,
                                         const ThingModelServiceRegistry &registry) const
{
    if (!request.target_found)
        return makeCommandError("UNKNOWN_DEVICE", "target device not found");

    if (request.method.empty())
        return makeCommandError("BAD_PARAMS", "missing method");

    const ServiceSpec *service = registry.findService(request.target_type, request.method);
    if (!service)
        return makeCommandError("UNKNOWN_SERVICE", "service not defined in thing model");

    for (const auto &param : service->input_params) {
        if (!request.params.contains(param.identifier)) {
            return makeCommandError("BAD_PARAMS", "missing param: " + param.identifier);
        }

        if (!paramTypeValid(request.params[param.identifier], param)) {
            return makeCommandError("BAD_PARAMS",
                                    "bad param type: " + param.identifier +
                                        " expect " + param.type);
        }
    }

    return makeCommandResult(true, "OK", "command validated");
}

bool CommandValidator::paramTypeValid(const nlohmann::json &value,
                                      const ServiceParamSpec &spec) const
{
    const std::string normalized = upperText(spec.type);
    if (normalized == "INT")
        return value.is_number_integer();
    if (normalized == "ENUM") {
        if (!value.is_number_integer())
            return false;
        if (spec.enum_values.empty())
            return true;
        const int64_t enum_value = value.get<int64_t>();
        return std::find(spec.enum_values.begin(), spec.enum_values.end(), enum_value) !=
               spec.enum_values.end();
    }
    if (normalized == "TEXT")
        return value.is_string();
    if (normalized == "BOOL" || normalized == "BOOLEAN")
        return value.is_boolean();
    if (normalized == "FLOAT" || normalized == "DOUBLE")
        return value.is_number();

    return true;
}

} // namespace gateway::command
