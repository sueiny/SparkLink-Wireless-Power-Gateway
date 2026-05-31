#pragma once

#include "common/logger.h"
#include "common/constants.h"
#include "storage/sqlite_db.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gateway::storage {

struct CachedTelemetry {
    int64_t id = 0;
    std::string topic;
    std::string payload;
    int64_t created_ts = 0;
};

// CacheStore 只负责本地遥测缓存读写，统一使用 gateway.db。
class CacheStore {
public:
    explicit CacheStore(log::Logger &logger);
    CacheStore(std::string db_path, int64_t ttl_ms, log::Logger &logger);

    // 打开 gateway.db 并初始化 telemetry_cache 表；数据库损坏时尝试从 .backup 恢复。
    bool init();

    // 追加一条 telemetry 缓存。采用追加写，避免发布失败高频发生时反复重写全文件。
    bool appendTelemetry(const std::string &topic, const std::string &payload);

    // 读取前 max_count 条待补传记录，同时记住本批记录 id，供 rewrite 精确删除。
    std::vector<CachedTelemetry> loadPendingTelemetry(size_t max_count) const;

    // 清理上一批 loadPendingTelemetry 读出的记录，再把发布失败的 remains 写回。
    // 未被读取的后续缓存不会受影响。
    bool rewritePendingTelemetry(const std::vector<CachedTelemetry> &remains);

    // 统计当前缓存行数。第一版按行统计，足够简单可靠。
    size_t pendingCount() const;

private:
    bool isExpired(const CachedTelemetry &item, int64_t now_ms) const;
    void pruneExpiredLocked() const;
    bool openSqliteWithRecovery();
    bool tryRecoverFromBackup();
    void backupIfNeededLocked() const;

    std::string db_path_;
    int64_t ttl_ms_ = common::kDefaultCacheTtlMs;
    log::Logger &logger_;
    std::unique_ptr<storage::SqliteDb> sqlite_;
    mutable std::mutex mutex_;
    mutable std::vector<int64_t> last_loaded_ids_;
    mutable int64_t last_backup_ts_ms_ = 0;
};

} // namespace gateway::storage
