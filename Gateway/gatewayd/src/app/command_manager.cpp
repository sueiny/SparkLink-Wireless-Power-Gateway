#include "app/command_manager.h"

#include "common/time_utils.h"

namespace gateway::app {
namespace {

constexpr int kCommandWaitMs = 500;

} // namespace

CommandManager::CommandManager(
    const config::AppConfig &config,
    log::Logger &logger,
    command::ThingModelServiceRegistry &service_registry,
    std::shared_ptr<state::DeviceStateStore> state_store,
    common::BlockingQueue<command::RawCommandMessage> &command_queue,
    common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue,
    common::BlockingQueue<PublishMessage> &publish_queue)
    : config_(config),
      logger_(logger),
      service_registry_(service_registry),
      state_store_(std::move(state_store)),
      command_queue_(command_queue),
      telemetry_queue_(telemetry_queue),
      publish_queue_(publish_queue)
{
}

void CommandManager::run()
{
    while (!stop_.load()) {
        command::RawCommandMessage raw;
        if (!command_queue_.pop(raw, kCommandWaitMs))
            continue;

        // Router 只处理协议形态。JSON 错误可以直接响应；未知 topic 则忽略。
        const auto parsed = command_router_.parse(raw, config_);
        if (parsed.has_immediate_response) {
            enqueuePublishMessage({
                parsed.immediate_response.topic,
                parsed.immediate_response.payload,
                PublishMessageKind::CommandResponse,
                0,
                0,
            });
            continue;
        }

        if (!parsed.has_request) {
            logger_.warn("CMD", "ignore unsupported command topic: " + raw.topic);
            continue;
        }

        logger_.info("CMD", "command received method=" + parsed.request.method +
                                ", target=" + parsed.request.target_device_id +
                                ", params=" + parsed.request.params.dump());

        // Validator 负责物模型层面的合法性，Executor 负责“是否接受执行”。
        // 当前 Executor 是模拟执行，真实 SLE/DTU 控制后续可替换在这一层。
        command::CommandResult result =
            command_validator_.validate(parsed.request, service_registry_);
        if (result.success) {
            result = command_executor_.execute(parsed.request);
            enqueueCommandStateTelemetry(parsed.request, result);
        }

        logger_.info("CMD", "command result method=" + parsed.request.method +
                                ", target=" + parsed.request.target_device_id +
                                ", code=" + result.code);
        const auto response = command_router_.buildResponse(parsed.request, result);
        enqueuePublishMessage({
            response.topic,
            response.payload,
            PublishMessageKind::CommandResponse,
            0,
            0,
        });
    }

    logger_.info("APP", "command thread stopped");
}

void CommandManager::enqueueCommandStateTelemetry(const command::CommandRequest &request,
                                                  const command::CommandResult &result)
{
    if (!result.success)
        return;

    model::TelemetryData telemetry;
    telemetry.device_id = request.target_device_id;
    telemetry.type = request.target_type;
    telemetry.ts_ms = common::nowMs();

    if (!state_patch_codec_.buildPatch(request, result, config_, &telemetry))
        return;

    // 先写状态库，再投递即时遥测。即使即时遥测发布失败，后续周期采集也会保持状态。
    if (state_store_ && !state_store_->applyPatch(telemetry))
        logger_.warn("STATE", "failed to persist command state patch");

    telemetry_queue_.push(std::vector<model::TelemetryData>{std::move(telemetry)});
    logger_.info("CMD", "command state telemetry enqueued method=" + request.method +
                            ", target=" + request.target_device_id);
}

void CommandManager::enqueuePublishMessage(PublishMessage message)
{
    publish_queue_.push(std::move(message));
}

} // namespace gateway::app
