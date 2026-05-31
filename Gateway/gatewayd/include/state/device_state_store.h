#pragma once

#include "json.hpp"
#include "common/logger.h"
#include "common/device_model.h"
#include "storage/sqlite_db.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace gateway::state {

// DeviceStateStore 保存云端命令模拟执行成功后的物模型状态，统一使用 gateway.db。
class DeviceStateStore {
public:
    // 打开 gateway.db，加载 device_state 表到内存；数据库损坏时尝试从 .backup 恢复。
    bool init(std::string db_path, log::Logger &logger);

    // 合并一条状态补丁并持久化。
    bool applyPatch(const model::TelemetryData &patch);

    // 采集出的模拟遥测在上报前会调用 overlay，把云端命令写入的状态覆盖进去。
    void overlay(model::TelemetryData &data) const;
    const std::string &dbPath() const { return db_path_; }

private:
    bool openSqliteWithRecovery();
    bool tryRecoverFromBackup();
    bool loadFromSqlite();
    bool persistLocked() const;
    void backupIfNeededLocked() const;

    std::string db_path_;
    log::Logger *logger_ = nullptr;
    std::unique_ptr<storage::SqliteDb> sqlite_;
    mutable std::mutex mutex_;
    std::map<std::string, nlohmann::json> states_;
    mutable int64_t last_backup_ts_ms_ = 0;
};

} // namespace gateway::state
