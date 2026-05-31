#include "command/command_executor.h"

namespace gateway::command {
namespace {

int jsonIntOr(const nlohmann::json &params, const char *key, int fallback)
{
    if (params.contains(key) && params[key].is_number_integer())
        return params[key].get<int>();
    return fallback;
}

} // namespace

CommandResult CommandExecutor::execute(const CommandRequest &request) const
{
    if (request.method == "set_relay") {
        return makeCommandResult(true, "OK", "relay command accepted",
                                 {{"result", 1},
                                  {"state", jsonIntOr(request.params, "state", 0)}});
    }

    if (request.method == "set_mode") {
        return makeCommandResult(true, "OK", "mode command accepted",
                                 {{"result", 1},
                                  {"mode", jsonIntOr(request.params, "mode", 0)}});
    }

    if (request.method == "set_collect_cycle") {
        return makeCommandResult(true, "OK", "collect cycle command accepted",
                                 {{"result", 1},
                                  {"cycle_ms", jsonIntOr(request.params, "cycle_ms", 0)}});
    }

    if (request.method == "trigger_collect") {
        return makeCommandResult(true, "OK", "collect command accepted", {{"result", 1}});
    }

    if (request.method == "reboot") {
        return makeCommandResult(true, "OK", "reboot command accepted", {{"result", 1}});
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

} // namespace gateway::command
