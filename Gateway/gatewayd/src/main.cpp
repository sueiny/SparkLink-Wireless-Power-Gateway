#include "app/gateway_app.h"
#include "cloud/mqtt_cloud_client.h"
#include "common/constants.h"
#include "common/time_utils.h"
#include "config/config_manager.h"
#include "common/logger.h"
#include "common/device_model.h"
#include "json.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic_bool g_quit{false};

struct CommandLineOptions {
    std::string config_path = gateway::common::kDefaultConfigPath;
    bool mqtt_test = false;
};

void handleSignal(int)
{
    g_quit.store(true);
}

void printUsage(const char *name)
{
    std::cout << "usage: " << name << " [--config path] [--mqtt-test]\n"
              << "default config: " << gateway::common::kDefaultConfigPath << "\n";
}

CommandLineOptions parseCommandLine(int argc, char *argv[])
{
    CommandLineOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            options.config_path = argv[++i];
        } else if (arg == "--mqtt-test") {
            options.mqtt_test = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            std::exit(2);
        }
    }

    return options;
}

int runMqttTest(const std::string &config_path)
{
    gateway::config::ConfigManager config_manager;
    std::string error;
    if (!config_manager.load(config_path, &error)) {
        std::cerr << error << "\n";
        return 1;
    }

    const auto &cfg = config_manager.config();
    gateway::log::Logger logger;
    if (!logger.init(cfg.log.dir)) {
        std::cerr << "failed to initialize logger: " << cfg.log.dir << "\n";
        return 1;
    }

    // MQTT 测试模式只验证云通道：连接 Broker，并发布一条固定 telemetry JSON。
    // 不启动 GatewayApp 主循环，也不采集模拟数据、不做缓存。
    gateway::cloud::MqttCloudClient client(cfg.thingskit, logger);
    if (!client.connect())
        return 1;

    gateway::model::GatewayStatus status;
    status.gateway_id = cfg.gateway.gateway_id;
    status.version = cfg.gateway.version;
    status.network_type = "mqtt_test";
    status.network_ifname = "unknown";
    status.cloud_connected = true;
    status.device_count = static_cast<int>(cfg.devices.size());
    status.cache_count = 0;
    status.ts_ms = gateway::common::nowMs();

    nlohmann::json thingskit_payload = {
        {"network_ifname", "wlan0"},
        {"cloud_connected", 1},
        {"device_count", status.device_count},
        {"cache_count", 0},
        {"gateway_version", cfg.gateway.version},
        {"network_type", "mqtt_test"},
    };
    const std::string telemetry_payload = thingskit_payload.dump();

    const bool telemetry_published = client.publishTelemetry(telemetry_payload);
    const bool attributes_published = client.publishAttributes(telemetry_payload);
    logger.info("MQTT", "thingskit telemetry payload=" + telemetry_payload);
    if (telemetry_published || attributes_published)
        std::this_thread::sleep_for(std::chrono::seconds(3));

    return telemetry_published && attributes_published ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    const CommandLineOptions options = parseCommandLine(argc, argv);

    // --mqtt-test 是独立诊断入口，只验证云连接和基础发布，不启动采集/命令/网络 worker。
    if (options.mqtt_test)
        return runMqttTest(options.config_path);

    // 正常模式下 GatewayApp 负责对象创建和线程编排；main 只处理命令行和信号。
    gateway::app::GatewayApp app(options.config_path);

    if (!app.init())
        return 1;

    return app.run(g_quit);
}
