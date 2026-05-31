#include "state/device_state_store.h"

#include "common/file_utils.h"
#include "common/time_utils.h"
#include "storage/sqlite_db.h"

#include <utility>

namespace gateway::state {
namespace {

constexpr int64_t kBackupIntervalMs = 5LL * 60LL * 1000LL;

std::string backupPath(const std::string &db_path)
{
    return db_path + ".backup";
}

} // namespace

bool DeviceStateStore::init(std::string db_path, log::Logger &logger)
{
    db_path_ = std::move(db_path);
    logger_ = &logger;
    sqlite_ = std::make_unique<storage::SqliteDb>();

    if (!openSqliteWithRecovery())
        return false;

    if (!loadFromSqlite())
        return false;

    backupIfNeededLocked();
    logger.info("STATE", "DeviceStateStore initialized with SQLite, db=" + db_path_);
    return true;
}

bool DeviceStateStore::openSqliteWithRecovery()
{
    std::string error;
    if (sqlite_->open(db_path_, &error) &&
        storage::initializeGatewaySchema(*sqlite_, &error)) {
        return true;
    }

    // 状态库只使用 SQLite；损坏时尝试从 .backup 恢复，失败则让应用启动失败。
    if (logger_)
        logger_->warn("STATE", "SQLite state open/schema failed, try recovery: " + error);
    sqlite_->close();
    if (!tryRecoverFromBackup())
        return false;

    error.clear();
    if (!sqlite_->open(db_path_, &error) ||
        !storage::initializeGatewaySchema(*sqlite_, &error)) {
        if (logger_)
            logger_->error("STATE", "SQLite state open failed after recovery: " + error);
        return false;
    }
    if (logger_)
        logger_->warn("STATE", "SQLite state recovered from backup");
    return true;
}

bool DeviceStateStore::tryRecoverFromBackup()
{
    const std::string backup_path = backupPath(db_path_);
    if (!common::regularFileExists(backup_path)) {
        if (logger_)
            logger_->error("STATE", "SQLite state recovery failed, backup not found: " + backup_path);
        return false;
    }

    common::unlinkIfExists(db_path_);
    common::unlinkIfExists(db_path_ + "-wal");
    common::unlinkIfExists(db_path_ + "-shm");
    if (!common::copyFile(backup_path, db_path_)) {
        if (logger_)
            logger_->error("STATE", "SQLite state recovery failed, copy backup failed: " + backup_path);
        return false;
    }
    return true;
}

bool DeviceStateStore::loadFromSqlite()
{
    std::lock_guard<std::mutex> lock(mutex_);
    states_.clear();

    std::string error;
    std::vector<storage::SqliteRow> rows;
    if (!sqlite_->query("SELECT device_id,values_json FROM device_state;", &rows, &error)) {
        if (logger_)
            logger_->error("STATE", "failed to load SQLite device state: " + error);
        return false;
    }

    for (const auto &row : rows) {
        if (row.columns.size() < 2)
            continue;
        try {
            const auto values = nlohmann::json::parse(row.columns[1]);
            if (values.is_object())
                states_[row.columns[0]] = values;
        } catch (const std::exception &e) {
            if (logger_)
                logger_->warn("STATE", std::string("skip broken SQLite state row: ") +
                                           e.what());
        }
    }
    return true;
}

bool DeviceStateStore::applyPatch(const model::TelemetryData &patch)
{
    if (patch.device_id.empty())
        return false;

    std::lock_guard<std::mutex> lock(mutex_);
    auto &values = states_[patch.device_id];
    if (!values.is_object())
        values = nlohmann::json::object();

    const auto patch_values = patch.toFlatJson();
    for (const auto &[key, value] : patch_values.items())
        values[key] = value;

    return persistLocked();
}

void DeviceStateStore::overlay(model::TelemetryData &data) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(data.device_id);
    if (it == states_.end() || !it->second.is_object())
        return;

    data.mergeFromFlatJson(it->second);
}

void DeviceStateStore::backupIfNeededLocked() const
{
    const int64_t now = common::nowMs();
    if (last_backup_ts_ms_ != 0 && now - last_backup_ts_ms_ < kBackupIntervalMs)
        return;

    last_backup_ts_ms_ = now;
    std::string error;
    if (!sqlite_->backupTo(backupPath(db_path_), &error) && logger_)
        logger_->warn("STATE", "SQLite backup failed: " + error);
}

bool DeviceStateStore::persistLocked() const
{
    std::string error;
    const bool ok = sqlite_->withTransaction([&]() {
        if (!sqlite_->exec("DELETE FROM device_state;", &error)) {
            if (logger_)
                logger_->error("STATE", "failed to clear SQLite device state: " + error);
            return false;
        }
        for (const auto &[device_id, values] : states_) {
            if (!sqlite_->upsertDeviceState(device_id, values.dump(), common::nowMs(), &error)) {
                if (logger_)
                    logger_->error("STATE", "failed to rewrite SQLite device state: " + error);
                return false;
            }
        }
        return true;
    }, &error);

    if (ok)
        backupIfNeededLocked();
    return ok;
}

} // namespace gateway::state
