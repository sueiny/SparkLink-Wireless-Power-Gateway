#include "rules/offline_rule_engine.h"

#include <sstream>
#include <utility>

namespace gateway::rules {
namespace {

bool getDoubleValue(const model::TelemetryData &data, const std::string &key, double *out)
{
    const auto numeric_it = data.numeric_values.find(key);
    if (numeric_it != data.numeric_values.end()) {
        *out = numeric_it->second;
        return true;
    }

    const auto integer_it = data.integer_values.find(key);
    if (integer_it != data.integer_values.end()) {
        *out = static_cast<double>(integer_it->second);
        return true;
    }

    return false;
}

std::string buildStateKey(const std::string &device_id, const std::string &rule_id)
{
    return device_id + ":" + rule_id;
}

std::string formatDouble(double value)
{
    std::ostringstream oss;
    oss.precision(3);
    oss << std::fixed << value;
    return oss.str();
}

nlohmann::json buildDetails(const std::string &rule_id,
                            double value,
                            double threshold,
                            const std::string &unit,
                            int64_t duration_ms)
{
    return {
        {"rule_id", rule_id},
        {"source", "offline_rule_engine"},
        {"offline", true},
        {"value", value},
        {"threshold", threshold},
        {"unit", unit},
        {"duration_ms", duration_ms},
    };
}

RuleEvent makeRuleEvent(const model::TelemetryData &data,
                        const std::string &rule_id,
                        const std::string &severity,
                        const std::string &message,
                        nlohmann::json details)
{
    RuleEvent event;
    event.device_id = data.device_id;
    event.event = rule_id;
    event.severity = severity;
    event.message = message;
    event.details = std::move(details);
    return event;
}

} // namespace

OfflineRuleEngine::OfflineRuleEngine(const config::AppConfig &config)
    : config_(config)
{
}

std::vector<RuleEvent> OfflineRuleEngine::evaluate(
    const std::vector<model::TelemetryData> &batch,
    bool offline_state,
    int64_t now_ms)
{
    std::vector<RuleEvent> events;
    const auto &analysis = config_.offline_analysis;
    if (!analysis.enable || !analysis.rule_engine.enable || !offline_state) {
        reset();
        return events;
    }

    for (const auto &item : batch) {
        updateDtuSeen(item, now_ms);
        switch (item.type) {
        case model::DeviceType::SinglePhaseMeter:
            evaluateMeter(item, now_ms, &events);
            break;
        case model::DeviceType::EnvSensor:
            evaluateEnv(item, now_ms, &events);
            break;
        case model::DeviceType::DtuNode:
            break;
        default:
            break;
        }
    }

    evaluateDtuOffline(now_ms, &events);
    return events;
}

config::MeterRuleConfig OfflineRuleEngine::meterConfigFor(const std::string &device_id) const
{
    auto result = config_.offline_analysis.rule_engine.meter;
    const auto it = config_.offline_analysis.rule_engine.device_overrides.find(device_id);
    if (it == config_.offline_analysis.rule_engine.device_overrides.end())
        return result;

    const auto &override = it->second;
    if (override.over_voltage_v > 0.0)
        result.over_voltage_v = override.over_voltage_v;
    if (override.under_voltage_v > 0.0)
        result.under_voltage_v = override.under_voltage_v;
    if (override.frequency_low_hz > 0.0)
        result.frequency_low_hz = override.frequency_low_hz;
    if (override.frequency_high_hz > 0.0)
        result.frequency_high_hz = override.frequency_high_hz;
    if (override.rated_current_a > 0.0)
        result.rated_current_a = override.rated_current_a;
    if (override.over_current_ratio > 0.0)
        result.over_current_ratio = override.over_current_ratio;
    if (override.hold_ms > 0)
        result.hold_ms = override.hold_ms;
    return result;
}

config::EnvRuleConfig OfflineRuleEngine::envConfigFor(const std::string &device_id) const
{
    auto result = config_.offline_analysis.rule_engine.env;
    const auto it = config_.offline_analysis.rule_engine.device_overrides.find(device_id);
    if (it == config_.offline_analysis.rule_engine.device_overrides.end())
        return result;

    const auto &override = it->second;
    if (override.high_temperature_c > 0.0)
        result.high_temperature_c = override.high_temperature_c;
    if (override.high_humidity_rh > 0.0)
        result.high_humidity_rh = override.high_humidity_rh;
    if (override.hold_ms > 0)
        result.hold_ms = override.hold_ms;
    return result;
}

config::DtuRuleConfig OfflineRuleEngine::dtuConfigFor(const std::string &device_id) const
{
    auto result = config_.offline_analysis.rule_engine.dtu;
    const auto it = config_.offline_analysis.rule_engine.device_overrides.find(device_id);
    if (it == config_.offline_analysis.rule_engine.device_overrides.end())
        return result;

    const auto &override = it->second;
    if (override.offline_timeout_ms > 0)
        result.offline_timeout_ms = override.offline_timeout_ms;
    return result;
}

void OfflineRuleEngine::evaluateMeter(const model::TelemetryData &data,
                                      int64_t now_ms,
                                      std::vector<RuleEvent> *events)
{
    const auto config = meterConfigFor(data.device_id);
    double value = 0.0;
    int64_t duration_ms = 0;

    if (getDoubleValue(data, "voltage", &value)) {
        if (updateRule(buildStateKey(data.device_id, "over_voltage"),
                       value > config.over_voltage_v,
                       config.hold_ms,
                       now_ms,
                       &duration_ms)) {
            events->push_back(makeRuleEvent(
                data,
                "over_voltage",
                "warning",
                "voltage " + formatDouble(value) + "V exceeds " +
                    formatDouble(config.over_voltage_v) + "V",
                buildDetails("over_voltage", value, config.over_voltage_v, "V", duration_ms)));
        }

        if (updateRule(buildStateKey(data.device_id, "under_voltage"),
                       value < config.under_voltage_v,
                       config.hold_ms,
                       now_ms,
                       &duration_ms)) {
            events->push_back(makeRuleEvent(
                data,
                "under_voltage",
                "warning",
                "voltage " + formatDouble(value) + "V below " +
                    formatDouble(config.under_voltage_v) + "V",
                buildDetails("under_voltage", value, config.under_voltage_v, "V", duration_ms)));
        }
    }

    if (getDoubleValue(data, "frequency", &value)) {
        const bool low = value < config.frequency_low_hz;
        const bool high = value > config.frequency_high_hz;
        if (updateRule(buildStateKey(data.device_id, "frequency_deviation"),
                       low || high,
                       config.hold_ms,
                       now_ms,
                       &duration_ms)) {
            auto details = buildDetails(
                "frequency_deviation",
                value,
                low ? config.frequency_low_hz : config.frequency_high_hz,
                "Hz",
                duration_ms);
            details["low_threshold"] = config.frequency_low_hz;
            details["high_threshold"] = config.frequency_high_hz;
            events->push_back(makeRuleEvent(
                data,
                "frequency_deviation",
                "warning",
                "frequency " + formatDouble(value) + "Hz outside configured range",
                std::move(details)));
        }
    }

    if (getDoubleValue(data, "current", &value)) {
        const double threshold = config.rated_current_a * config.over_current_ratio;
        if (updateRule(buildStateKey(data.device_id, "over_current"),
                       value > threshold,
                       config.hold_ms,
                       now_ms,
                       &duration_ms)) {
            auto details = buildDetails("over_current", value, threshold, "A", duration_ms);
            details["rated_current_a"] = config.rated_current_a;
            details["over_current_ratio"] = config.over_current_ratio;
            events->push_back(makeRuleEvent(
                data,
                "over_current",
                "critical",
                "current " + formatDouble(value) + "A exceeds " +
                    formatDouble(threshold) + "A",
                std::move(details)));
        }
    }
}

void OfflineRuleEngine::evaluateEnv(const model::TelemetryData &data,
                                    int64_t now_ms,
                                    std::vector<RuleEvent> *events)
{
    const auto config = envConfigFor(data.device_id);
    double value = 0.0;
    int64_t duration_ms = 0;

    if (getDoubleValue(data, "temperature", &value) &&
        updateRule(buildStateKey(data.device_id, "high_temperature"),
                   value >= config.high_temperature_c,
                   config.hold_ms,
                   now_ms,
                   &duration_ms)) {
        events->push_back(makeRuleEvent(
            data,
            "high_temperature",
            "warning",
            "temperature " + formatDouble(value) + "C reaches " +
                formatDouble(config.high_temperature_c) + "C",
            buildDetails("high_temperature", value, config.high_temperature_c, "C", duration_ms)));
    }

    if (getDoubleValue(data, "humidity", &value) &&
        updateRule(buildStateKey(data.device_id, "high_humidity"),
                   value >= config.high_humidity_rh,
                   config.hold_ms,
                   now_ms,
                   &duration_ms)) {
        events->push_back(makeRuleEvent(
            data,
            "high_humidity",
            "warning",
            "humidity " + formatDouble(value) + "%RH reaches " +
                formatDouble(config.high_humidity_rh) + "%RH",
            buildDetails("high_humidity", value, config.high_humidity_rh, "%RH", duration_ms)));
    }
}

void OfflineRuleEngine::updateDtuSeen(const model::TelemetryData &data, int64_t now_ms)
{
    if (data.type == model::DeviceType::DtuNode) {
        dtu_last_seen_ms_[data.device_id] = now_ms;
        return;
    }

    const auto dtu_it = data.integer_values.find("dtu_id");
    if (dtu_it == data.integer_values.end() || dtu_it->second <= 0)
        return;

    for (const auto &device : config_.dtu_devices) {
        if (device.node_id == dtu_it->second) {
            dtu_last_seen_ms_[device.device_id] = now_ms;
            return;
        }
    }
}

void OfflineRuleEngine::evaluateDtuOffline(int64_t now_ms, std::vector<RuleEvent> *events)
{
    for (const auto &device : config_.dtu_devices) {
        auto &last_seen = dtu_last_seen_ms_[device.device_id];
        if (last_seen == 0) {
            last_seen = now_ms;
            continue;
        }

        const auto config = dtuConfigFor(device.device_id);
        const int64_t offline_ms = now_ms - last_seen;
        int64_t duration_ms = 0;
        model::TelemetryData synthetic;
        synthetic.device_id = device.device_id;
        synthetic.type = model::DeviceType::DtuNode;
        synthetic.ts_ms = now_ms;
        if (updateRule(buildStateKey(device.device_id, "node_offline"),
                       offline_ms >= config.offline_timeout_ms,
                       1,
                       now_ms,
                       &duration_ms)) {
            auto details = buildDetails(
                "node_offline",
                static_cast<double>(offline_ms),
                static_cast<double>(config.offline_timeout_ms),
                "ms",
                offline_ms);
            events->push_back(makeRuleEvent(
                synthetic,
                "node_offline",
                "critical",
                "DTU node has no heartbeat for " + std::to_string(offline_ms) + "ms",
                std::move(details)));
        }
    }
}

bool OfflineRuleEngine::updateRule(const std::string &state_key,
                                   bool condition,
                                   int hold_ms,
                                   int64_t now_ms,
                                   int64_t *duration_ms)
{
    auto &state = states_[state_key];
    if (!condition) {
        state.condition_since_ms = 0;
        if (state.active) {
            if (state.normal_since_ms == 0)
                state.normal_since_ms = now_ms;
            if (now_ms - state.normal_since_ms >= config_.offline_analysis.exit_hold_ms) {
                state.active = false;
                state.normal_since_ms = 0;
            }
        } else {
            state.normal_since_ms = 0;
        }
        return false;
    }

    state.normal_since_ms = 0;
    if (state.condition_since_ms == 0)
        state.condition_since_ms = now_ms;

    const int64_t active_ms = now_ms - state.condition_since_ms;
    if (duration_ms)
        *duration_ms = active_ms;

    const int64_t cooldown_ms = config_.offline_analysis.rule_engine.cooldown_ms;
    const bool cooldown_ok =
        state.last_event_ms == 0 || now_ms - state.last_event_ms >= cooldown_ms;
    if (active_ms >= hold_ms && cooldown_ok) {
        state.active = true;
        state.last_event_ms = now_ms;
        return true;
    }

    return false;
}

void OfflineRuleEngine::reset()
{
    states_.clear();
    dtu_last_seen_ms_.clear();
}

} // namespace gateway::rules
