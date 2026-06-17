#pragma once

#include "common/device_model.h"
#include "common/logger.h"
#include "config/config_manager.h"
#include "json.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace gateway::ai {

struct AiRiskEvent {
    std::string device_id;
    std::string risk_type;
    std::string risk_level;
    std::string message;
    nlohmann::json details = nlohmann::json::object();
};

struct OfflineAiEvaluation {
    std::vector<AiRiskEvent> events;
};

// 离线 AI 分析器只消费标准化 TelemetryData。
// 首版加载 PC 侧训练导出的线性风险模型 JSON，不依赖 Python/scikit runtime。
class OfflineAiAnalyzer {
public:
    OfflineAiAnalyzer(const config::AppConfig &config, log::Logger &logger);

    bool init();
    OfflineAiEvaluation evaluate(const std::vector<model::TelemetryData> &batch,
                                 bool offline_state,
                                 int64_t now_ms);

private:
    struct Sample {
        int64_t ts_ms = 0;
        model::DeviceType type = model::DeviceType::Gateway;
        std::map<std::string, double> values;
    };

    struct ModelHead {
        std::string risk_type;
        std::vector<model::DeviceType> device_types;
        std::map<std::string, double> weights;
        double bias = 0.0;
    };

    bool loadModel(const std::string &path);
    bool headMatchesDevice(const ModelHead &head, model::DeviceType type) const;
    void updateHistory(const model::TelemetryData &data, int64_t now_ms);
    std::map<std::string, double> buildFeatures(const model::TelemetryData &data,
                                                const std::deque<Sample> &history) const;
    double normalizedFeature(const std::string &name, double value) const;
    double scoreHead(const ModelHead &head,
                     const std::map<std::string, double> &features) const;
    void evaluateDtuStability(int64_t now_ms, OfflineAiEvaluation *evaluation);
    bool cooldownReady(const std::string &device_id,
                       const std::string &risk_type,
                       int64_t now_ms);
    void reset();

    const config::AppConfig &config_;
    log::Logger &logger_;
    bool initialized_ = false;
    bool enabled_ = false;
    std::string model_version_;
    std::vector<std::string> feature_names_;
    std::map<std::string, double> feature_mean_;
    std::map<std::string, double> feature_scale_;
    std::vector<ModelHead> heads_;
    std::map<std::string, std::deque<Sample>> histories_;
    std::map<std::string, int64_t> last_event_ms_;
};

} // namespace gateway::ai
