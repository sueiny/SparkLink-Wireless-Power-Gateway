#include "ai/offline_ai_analyzer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace gateway::ai {
namespace {

constexpr double kDefaultNominalVoltage = 220.0;
constexpr double kDefaultNominalFrequency = 50.0;

double valueOrZero(const std::map<std::string, double> &values, const std::string &key)
{
    const auto it = values.find(key);
    return it == values.end() ? 0.0 : it->second;
}

double telemetryValue(const model::TelemetryData &data, const std::string &key)
{
    const auto numeric_it = data.numeric_values.find(key);
    if (numeric_it != data.numeric_values.end())
        return numeric_it->second;

    const auto integer_it = data.integer_values.find(key);
    if (integer_it != data.integer_values.end())
        return static_cast<double>(integer_it->second);

    const auto bool_it = data.bool_values.find(key);
    if (bool_it != data.bool_values.end())
        return bool_it->second ? 1.0 : 0.0;

    return 0.0;
}

std::vector<model::DeviceType> parseDeviceTypes(const nlohmann::json &item)
{
    std::vector<model::DeviceType> result;
    if (!item.is_array())
        return result;

    for (const auto &value : item) {
        if (value.is_string())
            result.push_back(model::deviceTypeFromString(value.get<std::string>()));
    }
    return result;
}

double jsonNumberOrDefault(const nlohmann::json &value, double default_value)
{
    try {
        if (value.is_number())
            return value.get<double>();
    } catch (const std::exception &) {
    }
    return default_value;
}

double sigmoid(double value)
{
    if (value >= 35.0)
        return 1.0;
    if (value <= -35.0)
        return 0.0;
    return 1.0 / (1.0 + std::exp(-value));
}

std::string riskLevel(double score, const config::AiConfig &config)
{
    if (score >= config.risk_threshold_high)
        return "high";
    if (score >= config.risk_threshold_medium)
        return "medium";
    return "normal";
}

nlohmann::json selectedFeatures(const std::map<std::string, double> &features,
                                const std::map<std::string, double> &weights)
{
    nlohmann::json values = nlohmann::json::object();
    for (const auto &item : weights) {
        const auto it = features.find(item.first);
        if (it != features.end())
            values[item.first] = it->second;
    }
    return values;
}

std::string buildMessage(const std::string &risk_type,
                         const std::string &risk_level,
                         double score)
{
    std::ostringstream oss;
    oss.precision(3);
    oss << std::fixed << "offline AI " << risk_type
        << " level=" << risk_level
        << " score=" << score;
    return oss.str();
}

} // namespace

OfflineAiAnalyzer::OfflineAiAnalyzer(const config::AppConfig &config, log::Logger &logger)
    : config_(config),
      logger_(logger)
{
}

bool OfflineAiAnalyzer::init()
{
    initialized_ = true;
    enabled_ = false;

    const auto &ai = config_.offline_analysis.ai;
    if (!config_.offline_analysis.enable || !ai.enable)
        return true;

    if (!loadModel(ai.model_path)) {
        logger_.warn("AI", "offline AI disabled, failed to load model path=" + ai.model_path);
        return true;
    }

    enabled_ = true;
    logger_.info("AI", "offline AI enabled model_version=" + model_version_ +
                         ", heads=" + std::to_string(heads_.size()) +
                         ", features=" + std::to_string(feature_names_.size()));
    return true;
}

bool OfflineAiAnalyzer::loadModel(const std::string &path)
{
    if (path.empty()) {
        logger_.warn("AI", "model_path is empty");
        return false;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        logger_.warn("AI", "failed to open model file: " + path);
        return false;
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const std::exception &e) {
        logger_.warn("AI", std::string("failed to parse model json: ") + e.what());
        return false;
    }

    if (!root.is_object())
        return false;

    try {
        model_version_ = root.value("version", "unknown");
        feature_names_.clear();
        const auto feature_names = root.value("feature_names", nlohmann::json::array());
        for (const auto &item : feature_names) {
            if (item.is_string())
                feature_names_.push_back(item.get<std::string>());
        }

        feature_mean_.clear();
        const auto feature_mean = root.value("feature_mean", nlohmann::json::object());
        for (const auto &item : feature_mean.items())
            feature_mean_[item.key()] = jsonNumberOrDefault(item.value(), 0.0);

        feature_scale_.clear();
        const auto feature_scale = root.value("feature_scale", nlohmann::json::object());
        for (const auto &item : feature_scale.items()) {
            const double value = jsonNumberOrDefault(item.value(), 1.0);
            feature_scale_[item.key()] = std::fabs(value) < 1e-9 ? 1.0 : value;
        }

        heads_.clear();
        const auto heads = root.value("heads", nlohmann::json::object());
        for (const auto &item : heads.items()) {
            if (!item.value().is_object())
                continue;

            ModelHead head;
            head.risk_type = item.key();
            head.bias = jsonNumberOrDefault(item.value().value("bias", nlohmann::json(0.0)), 0.0);
            head.device_types = parseDeviceTypes(item.value().value("device_types", nlohmann::json::array()));
            const auto weights = item.value().value("weights", nlohmann::json::object());
            for (const auto &weight : weights.items()) {
                if (weight.value().is_number())
                    head.weights[weight.key()] = jsonNumberOrDefault(weight.value(), 0.0);
            }
            if (!head.weights.empty())
                heads_.push_back(std::move(head));
        }
    } catch (const std::exception &e) {
        logger_.warn("AI", std::string("invalid model json schema: ") + e.what());
        return false;
    }

    return !feature_names_.empty() && !heads_.empty();
}

OfflineAiEvaluation OfflineAiAnalyzer::evaluate(const std::vector<model::TelemetryData> &batch,
                                                bool offline_state,
                                                int64_t now_ms)
{
    OfflineAiEvaluation evaluation;
    if (!initialized_)
        init();

    if (!enabled_ || !offline_state) {
        if (!offline_state)
            reset();
        return evaluation;
    }

    for (const auto &item : batch)
        updateHistory(item, now_ms);

    for (const auto &item : batch) {
        const auto history_it = histories_.find(item.device_id);
        if (history_it == histories_.end())
            continue;
        if (static_cast<int>(history_it->second.size()) < config_.offline_analysis.ai.min_samples)
            continue;

        const auto features = buildFeatures(item, history_it->second);
        for (const auto &head : heads_) {
            if (!headMatchesDevice(head, item.type))
                continue;

            const double score = scoreHead(head, features);
            if (score < config_.offline_analysis.ai.risk_threshold_medium)
                continue;
            if (!cooldownReady(item.device_id, head.risk_type, now_ms))
                continue;

            const std::string level = riskLevel(score, config_.offline_analysis.ai);
            AiRiskEvent event;
            event.device_id = item.device_id;
            event.risk_type = head.risk_type;
            event.risk_level = level;
            event.message = buildMessage(head.risk_type, level, score);
            event.details = {
                {"source", "offline_ai"},
                {"offline", true},
                {"model_version", model_version_},
                {"mode", config_.offline_analysis.ai.mode},
                {"risk_type", head.risk_type},
                {"risk_score", score},
                {"risk_level", level},
                {"window_ms", config_.offline_analysis.ai.window_ms},
                {"sample_count", history_it->second.size()},
                {"features", selectedFeatures(features, head.weights)},
            };
            evaluation.events.push_back(std::move(event));
        }
    }

    evaluateDtuStability(now_ms, &evaluation);
    return evaluation;
}

bool OfflineAiAnalyzer::headMatchesDevice(const ModelHead &head, model::DeviceType type) const
{
    if (head.device_types.empty())
        return true;
    return std::find(head.device_types.begin(), head.device_types.end(), type) !=
           head.device_types.end();
}

void OfflineAiAnalyzer::updateHistory(const model::TelemetryData &data, int64_t now_ms)
{
    Sample sample;
    sample.ts_ms = data.ts_ms > 0 ? data.ts_ms : now_ms;
    sample.type = data.type;
    sample.values = data.numeric_values;
    for (const auto &item : data.integer_values)
        sample.values[item.first] = static_cast<double>(item.second);
    for (const auto &item : data.bool_values)
        sample.values[item.first] = item.second ? 1.0 : 0.0;

    auto &history = histories_[data.device_id];
    history.push_back(std::move(sample));

    const int64_t cutoff = now_ms - config_.offline_analysis.ai.window_ms;
    while (!history.empty() && history.front().ts_ms < cutoff)
        history.pop_front();
}

std::map<std::string, double> OfflineAiAnalyzer::buildFeatures(
    const model::TelemetryData &data,
    const std::deque<Sample> &history) const
{
    std::map<std::string, double> features;
    const auto &last = history.back();
    const auto &first = history.front();
    const double span_min = std::max(1.0, static_cast<double>(last.ts_ms - first.ts_ms) / 60000.0);

    const double voltage = telemetryValue(data, "voltage");
    const double current = telemetryValue(data, "current");
    const double active_power = telemetryValue(data, "active_power");
    const double power_factor = telemetryValue(data, "power_factor");
    const double frequency = telemetryValue(data, "frequency");
    const double energy = telemetryValue(data, "energy");
    const double temperature = telemetryValue(data, "temperature");
    const double humidity = telemetryValue(data, "humidity");
    const double relay_state = telemetryValue(data, "relay_state") + telemetryValue(data, "relay_status");
    const double online = telemetryValue(data, "online");

    features["voltage"] = voltage;
    features["current"] = current;
    features["active_power"] = active_power;
    features["power_factor"] = power_factor;
    features["frequency"] = frequency;
    features["energy"] = energy;
    features["temperature"] = temperature;
    features["humidity"] = humidity;
    features["relay_state"] = relay_state > 0.0 ? 1.0 : 0.0;
    features["online"] = online;

    features["voltage_deviation_ratio"] =
        voltage > 0.0 ? std::fabs(voltage - kDefaultNominalVoltage) / kDefaultNominalVoltage : 0.0;
    features["frequency_deviation_hz"] =
        frequency > 0.0 ? std::fabs(frequency - kDefaultNominalFrequency) : 0.0;
    features["power_factor_drop"] =
        power_factor > 0.0 ? std::max(0.0, 1.0 - power_factor) : 0.0;

    const double first_voltage = valueOrZero(first.values, "voltage");
    const double first_current = valueOrZero(first.values, "current");
    const double first_temperature = valueOrZero(first.values, "temperature");
    const double first_humidity = valueOrZero(first.values, "humidity");
    const double first_energy = valueOrZero(first.values, "energy");

    features["voltage_slope_per_min"] = (voltage - first_voltage) / span_min;
    features["current_slope_per_min"] = (current - first_current) / span_min;
    features["temperature_slope_per_min"] = (temperature - first_temperature) / span_min;
    features["humidity_slope_per_min"] = (humidity - first_humidity) / span_min;
    features["energy_delta"] = energy - first_energy;
    features["energy_freeze"] = (energy > 0.0 && std::fabs(energy - first_energy) < 1e-6) ? 1.0 : 0.0;

    if (history.size() >= 2) {
        const auto &prev = history[history.size() - 2];
        const double gap_ms = static_cast<double>(last.ts_ms - prev.ts_ms);
        const double expected_gap = static_cast<double>(
            config_.publish.interval_ms > 0 ? config_.publish.interval_ms : 5000);
        features["sample_gap_ms"] = gap_ms;
        features["sample_gap_ratio"] = expected_gap > 0.0 ? gap_ms / expected_gap : 0.0;
    } else {
        features["sample_gap_ms"] = 0.0;
        features["sample_gap_ratio"] = 0.0;
    }

    return features;
}

double OfflineAiAnalyzer::normalizedFeature(const std::string &name, double value) const
{
    const auto mean_it = feature_mean_.find(name);
    const auto scale_it = feature_scale_.find(name);
    const double mean = mean_it == feature_mean_.end() ? 0.0 : mean_it->second;
    const double scale = scale_it == feature_scale_.end() ? 1.0 : scale_it->second;
    return (value - mean) / scale;
}

double OfflineAiAnalyzer::scoreHead(const ModelHead &head,
                                    const std::map<std::string, double> &features) const
{
    double sum = head.bias;
    for (const auto &item : head.weights) {
        const auto it = features.find(item.first);
        if (it != features.end())
            sum += item.second * normalizedFeature(item.first, it->second);
    }
    return sigmoid(sum);
}

void OfflineAiAnalyzer::evaluateDtuStability(int64_t now_ms, OfflineAiEvaluation *evaluation)
{
    if (evaluation == nullptr)
        return;

    for (const auto &item : histories_) {
        if (item.second.empty() || item.second.back().type != model::DeviceType::DtuNode)
            continue;
        if (static_cast<int>(item.second.size()) < config_.offline_analysis.ai.min_samples)
            continue;

        model::TelemetryData synthetic;
        synthetic.device_id = item.first;
        synthetic.type = model::DeviceType::DtuNode;
        synthetic.ts_ms = now_ms;
        synthetic.integer_values["online"] =
            (now_ms - item.second.back().ts_ms <= config_.offline_analysis.ai.window_ms) ? 1 : 0;

        auto features = buildFeatures(synthetic, item.second);
        const double gap_ms = static_cast<double>(now_ms - item.second.back().ts_ms);
        const double expected_gap = static_cast<double>(
            config_.publish.interval_ms > 0 ? config_.publish.interval_ms : 5000);
        features["sample_gap_ms"] = std::max(features["sample_gap_ms"], gap_ms);
        features["sample_gap_ratio"] = expected_gap > 0.0 ? features["sample_gap_ms"] / expected_gap : 0.0;

        for (const auto &head : heads_) {
            if (head.risk_type != "dtu_stability_risk")
                continue;
            if (!headMatchesDevice(head, model::DeviceType::DtuNode))
                continue;

            const double score = scoreHead(head, features);
            if (score < config_.offline_analysis.ai.risk_threshold_medium)
                continue;
            if (!cooldownReady(item.first, head.risk_type, now_ms))
                continue;

            const std::string level = riskLevel(score, config_.offline_analysis.ai);
            AiRiskEvent event;
            event.device_id = item.first;
            event.risk_type = head.risk_type;
            event.risk_level = level;
            event.message = buildMessage(head.risk_type, level, score);
            event.details = {
                {"source", "offline_ai"},
                {"offline", true},
                {"model_version", model_version_},
                {"mode", config_.offline_analysis.ai.mode},
                {"risk_type", head.risk_type},
                {"risk_score", score},
                {"risk_level", level},
                {"window_ms", config_.offline_analysis.ai.window_ms},
                {"sample_count", item.second.size()},
                {"features", selectedFeatures(features, head.weights)},
            };
            evaluation->events.push_back(std::move(event));
        }
    }
}

bool OfflineAiAnalyzer::cooldownReady(const std::string &device_id,
                                      const std::string &risk_type,
                                      int64_t now_ms)
{
    const std::string key = device_id + ":" + risk_type;
    const auto it = last_event_ms_.find(key);
    if (it != last_event_ms_.end() &&
        now_ms - it->second < config_.offline_analysis.ai.cooldown_ms)
        return false;
    last_event_ms_[key] = now_ms;
    return true;
}

void OfflineAiAnalyzer::reset()
{
    histories_.clear();
    last_event_ms_.clear();
}

} // namespace gateway::ai
