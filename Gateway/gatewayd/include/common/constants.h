#pragma once

#include <cstdint>

namespace gateway::common {

constexpr int64_t kDefaultCacheTtlMs = 7LL * 24LL * 60LL * 60LL * 1000LL;

constexpr const char *kGatewayBasePath = "/userdata/gateway";
constexpr const char *kDefaultConfigPath = "/userdata/gateway/config/gateway_config.json";
constexpr const char *kDefaultDbPath = "/userdata/gateway/data/gateway.db";
constexpr const char *kDefaultLogDir = "/userdata/gateway/data/log";
constexpr const char *kDefaultThingsModelDir = "/userdata/gateway/things_model";

} // namespace gateway::common
