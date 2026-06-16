#pragma once

#include "common/device_model.h"
#include "config/config_manager.h"
#include "json.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gateway::rules {

struct RuleEvent {
    std::string device_id;
    std::string event;
    std::string severity;
    std::string message;
    nlohmann::json details = nlohmann::json::object();
};

// 离线规则引擎只做纯计算：输入标准化 TelemetryData，输出待发布事件。
// 它不处理 MQTT、不写 SQLite，也不解析 SLE/Modbus 原始帧。
class OfflineRuleEngine {
public:
    explicit OfflineRuleEngine(const config::AppConfig &config);

    std::vector<RuleEvent> evaluate(const std::vector<model::TelemetryData> &batch,
                                    bool offline_state,
                                    int64_t now_ms);

private:
    struct RuleState {
        int64_t condition_since_ms = 0;
        int64_t normal_since_ms = 0;
        int64_t last_event_ms = 0;
        bool active = false;
    };

    config::MeterRuleConfig meterConfigFor(const std::string &device_id) const;
    config::EnvRuleConfig envConfigFor(const std::string &device_id) const;
    config::DtuRuleConfig dtuConfigFor(const std::string &device_id) const;

    void evaluateMeter(const model::TelemetryData &data,
                       int64_t now_ms,
                       std::vector<RuleEvent> *events);
    void evaluateEnv(const model::TelemetryData &data,
                     int64_t now_ms,
                     std::vector<RuleEvent> *events);
    void updateDtuSeen(const model::TelemetryData &data, int64_t now_ms);
    void evaluateDtuOffline(int64_t now_ms, std::vector<RuleEvent> *events);

    bool updateRule(const std::string &state_key,
                    bool condition,
                    int hold_ms,
                    int64_t now_ms,
                    int64_t *duration_ms);

    void reset();

    const config::AppConfig &config_;
    std::map<std::string, RuleState> states_;
    std::map<std::string, int64_t> dtu_last_seen_ms_;
};

} // namespace gateway::rules
