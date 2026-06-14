#pragma once

#include "app/publish_types.h"
#include "app/worker_base.h"
#include "command/command_executor.h"
#include "command/command_router.h"
#include "command/command_validator.h"
#include "command/thing_model_service_registry.h"
#include "common/blocking_queue.h"
#include "common/logger.h"
#include "config/config_manager.h"
#include "state/device_state_store.h"
#include "state/state_patch_codec.h"

#include <memory>
#include <vector>

namespace gateway::datasource {
class IpcCmdSender;
} // namespace gateway::datasource

namespace gateway::app {

// 命令线程入口。
//
// 数据流：
// MQTT 回调 -> RawCommandMessage 队列 -> CommandManager ->
// Router 解析 topic/payload -> Validator 按物模型校验 -> Executor 模拟执行 ->
// StatePatchCodec 生成状态补丁 -> DeviceStateStore 持久化 -> 即时遥测入队 ->
// 命令响应入 publish_queue。
//
// 这里故意不直接 publish MQTT，避免命令线程和发布线程同时操作云连接。
class CommandManager final : public WorkerBase<CommandManager> {
public:
    CommandManager(const config::AppConfig &config,
                   log::Logger &logger,
                   command::ThingModelServiceRegistry &service_registry,
                   std::shared_ptr<state::DeviceStateStore> state_store,
                   common::BlockingQueue<command::RawCommandMessage> &command_queue,
                   common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue,
                   common::BlockingQueue<PublishMessage> &publish_queue,
                   datasource::IpcCmdSender *ipc_cmd_sender = nullptr);

    const char *name() const override { return "command"; }

private:
    friend class WorkerBase<CommandManager>;
    void run();

    // 命令成功后把“模拟控制结果”转成物模型属性补丁。
    // 例如 RELAY_001 relay_state=1，既写入状态库，也立刻生成一条遥测。
    void enqueueCommandStateTelemetry(const command::CommandRequest &request,
                                      const command::CommandResult &result);

    // 统一进入发布队列，由 PublishManager 决定连接、重试和丢弃策略。
    void enqueuePublishMessage(PublishMessage message);

    const config::AppConfig &config_;
    log::Logger &logger_;
    command::ThingModelServiceRegistry &service_registry_;
    std::shared_ptr<state::DeviceStateStore> state_store_;
    common::BlockingQueue<command::RawCommandMessage> &command_queue_;
    common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue_;
    common::BlockingQueue<PublishMessage> &publish_queue_;

    command::CommandRouter command_router_;
    command::CommandValidator command_validator_;
    command::CommandExecutor command_executor_;
    codec::StatePatchCodec state_patch_codec_;
};

} // namespace gateway::app
