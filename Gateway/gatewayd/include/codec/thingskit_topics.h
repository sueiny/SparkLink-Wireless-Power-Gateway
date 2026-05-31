#pragma once

namespace gateway::codec::thingskit {

inline constexpr const char *kGatewaySubDeviceTelemetryTopic = "v1/gateway/telemetry";
inline constexpr const char *kGatewayTelemetryTopic = "v1/devices/me/telemetry";
inline constexpr const char *kGatewayAttributesTopic = "v1/devices/me/attributes";
inline constexpr const char *kGatewayCommandRequestTopic = "v1/gateway/commands/request";
inline constexpr const char *kGatewayCommandResponseTopic = "v1/gateway/commands/response";
inline constexpr const char *kRpcRequestTopicPrefix = "v1/devices/me/rpc/request/";
inline constexpr const char *kRpcRequestTopicFilter = "v1/devices/me/rpc/request/+";
inline constexpr const char *kRpcResponseTopicPrefix = "v1/devices/me/rpc/response/";

} // namespace gateway::codec::thingskit
