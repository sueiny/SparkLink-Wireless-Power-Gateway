#include "storage/cache_store.h"

#include "common/file_utils.h"
#include "common/time_utils.h"
#include "storage/sqlite_db.h"

#include <exception>
#include <limits>
#include <sstream>
#include <utility>

namespace gateway::storage {
namespace {

constexpr int64_t kBackupIntervalMs = 5LL * 60LL * 1000LL;

std::string backupPath(const std::string &db_path)
{
    return db_path + ".backup";
}

std::string idListSql(const std::vector<int64_t> &ids)
{
    std::ostringstream ss;
    for (size_t index = 0; index < ids.size(); ++index) {
        if (index > 0)
            ss << ",";
        ss << ids[index];
    }
    return ss.str();
}

} // namespace

CacheStore::CacheStore(log::Logger &logger)
    : CacheStore(common::kDefaultDbPath, common::kDefaultCacheTtlMs, logger)
{
}

CacheStore::CacheStore(std::string db_path, int64_t ttl_ms, log::Logger &logger)
    : db_path_(std::move(db_path)),
      ttl_ms_(ttl_ms > 0 ? ttl_ms : common::kDefaultCacheTtlMs),
      logger_(logger)
{
}

bool CacheStore::isExpired(const CachedTelemetry &item, int64_t now_ms) const
{
    return item.created_ts > 0 && ttl_ms_ > 0 && now_ms - item.created_ts > ttl_ms_;
}

void CacheStore::pruneExpiredLocked() const
{
    if (!sqlite_ || ttl_ms_ <= 0)
        return;

    std::string error;
    const int64_t expire_before = common::nowMs() - ttl_ms_;
    if (!sqlite_->exec("DELETE FROM telemetry_cache WHERE created_ts<" +
                           std::to_string(expire_before) + ";",
                       &error)) {
        logger_.warn("CACHE", "failed to prune expired SQLite telemetry cache: " + error);
    }
}

bool CacheStore::init()
{
    sqlite_ = std::make_unique<storage::SqliteDb>();
    if (!openSqliteWithRecovery())
        return false;

    backupIfNeededLocked();
    logger_.info("CACHE", "CacheStore initialized with SQLite, db=" + db_path_);
    return true;
}

bool CacheStore::openSqliteWithRecovery()
{
    std::string error;
    if (sqlite_->open(db_path_, &error) &&
        storage::initializeGatewaySchema(*sqlite_, &error)) {
        return true;
    }

    // SQLite-only 后不再回退 JSONL。主库打不开时只能尝试 .backup，失败则启动失败。
    logger_.warn("CACHE", "SQLite cache open/schema failed, try recovery: " + error);
    sqlite_->close();
    if (!tryRecoverFromBackup())
        return false;

    error.clear();
    if (!sqlite_->open(db_path_, &error) ||
        !storage::initializeGatewaySchema(*sqlite_, &error)) {
        logger_.error("CACHE", "SQLite cache open failed after recovery: " + error);
        return false;
    }
    logger_.warn("CACHE", "SQLite cache recovered from backup");
    return true;
}

bool CacheStore::tryRecoverFromBackup()
{
    const std::string backup_path = backupPath(db_path_);
    if (!common::regularFileExists(backup_path)) {
        logger_.error("CACHE", "SQLite cache recovery failed, backup not found: " + backup_path);
        return false;
    }

    common::unlinkIfExists(db_path_);
    common::unlinkIfExists(db_path_ + "-wal");
    common::unlinkIfExists(db_path_ + "-shm");
    if (!common::copyFile(backup_path, db_path_)) {
        logger_.error("CACHE", "SQLite cache recovery failed, copy backup failed: " + backup_path);
        return false;
    }
    return true;
}

void CacheStore::backupIfNeededLocked() const
{
    const int64_t now = common::nowMs();
    if (last_backup_ts_ms_ != 0 && now - last_backup_ts_ms_ < kBackupIntervalMs)
        return;

    last_backup_ts_ms_ = now;
    std::string error;
    if (!sqlite_->backupTo(backupPath(db_path_), &error))
        logger_.warn("CACHE", "SQLite backup failed: " + error);
}

bool CacheStore::appendTelemetry(const std::string &topic, const std::string &payload)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (topic.empty() || payload.empty()) {
        logger_.warn("CACHE", "skip empty telemetry cache record");
        return false;
    }

    std::string error;
    if (!sqlite_->insertTelemetry(topic, payload, common::nowMs(), &error)) {
        logger_.error("CACHE", "failed to append SQLite telemetry cache: " + error);
        return false;
    }

    backupIfNeededLocked();
    logger_.info("CACHE", "telemetry cached topic=" + topic +
                              ", bytes=" + std::to_string(payload.size()));
    return true;
}

std::vector<CachedTelemetry> CacheStore::loadPendingTelemetry(size_t max_count) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CachedTelemetry> items;
    last_loaded_ids_.clear();

    if (max_count == 0)
        return items;

    pruneExpiredLocked();

    std::ostringstream sql;
    sql << "SELECT id,topic,payload,created_ts FROM telemetry_cache "
        << "WHERE created_ts>=" << (common::nowMs() - ttl_ms_) << " ORDER BY id";
    if (max_count != std::numeric_limits<size_t>::max())
        sql << " LIMIT " << max_count;
    sql << ";";

    std::string error;
    std::vector<storage::SqliteRow> rows;
    if (!sqlite_->query(sql.str(), &rows, &error)) {
        logger_.error("CACHE", "failed to load SQLite telemetry cache: " + error);
        return items;
    }

    items.reserve(rows.size());
    last_loaded_ids_.reserve(rows.size());
    const int64_t now = common::nowMs();
    for (const auto &row : rows) {
        if (row.columns.size() < 4)
            continue;

        CachedTelemetry item;
        try {
            item.id = std::stoll(row.columns[0]);
            item.created_ts = std::stoll(row.columns[3]);
        } catch (const std::exception &e) {
            logger_.warn("CACHE", "skip invalid SQLite telemetry row: " + std::string(e.what()));
            continue;
        }
        item.topic = row.columns[1];
        item.payload = row.columns[2];
        if (!isExpired(item, now)) {
            // 记住本次加载到的 id，rewrite 时只处理这一批，避免删除未加载的后续缓存。
            last_loaded_ids_.push_back(item.id);
            items.push_back(std::move(item));
        }
    }
    return items;
}

bool CacheStore::rewritePendingTelemetry(const std::vector<CachedTelemetry> &remains)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pruneExpiredLocked();

    std::string error;
    const int64_t now = common::nowMs();
    const auto loaded_ids = last_loaded_ids_;

    // 只删除本批已加载记录，再把失败记录重新插入队尾。
    // 这比“清空全表再写回 remains”安全，尤其是在补传批量小于缓存总量时。
    const bool ok = sqlite_->withTransaction([&]() {
        if (!loaded_ids.empty()) {
            if (!sqlite_->exec("DELETE FROM telemetry_cache WHERE id IN (" +
                                   idListSql(loaded_ids) + ");",
                               &error)) {
                logger_.error("CACHE", "failed to clear SQLite telemetry batch: " + error);
                return false;
            }
        }
        for (const auto &item : remains) {
            if (isExpired(item, now))
                continue;
            if (!sqlite_->insertTelemetry(item.topic, item.payload, item.created_ts, &error)) {
                logger_.error("CACHE", "failed to rewrite SQLite telemetry cache: " + error);
                return false;
            }
        }
        return true;
    }, &error);

    if (ok) {
        last_loaded_ids_.clear();
        backupIfNeededLocked();
    }
    return ok;
}

size_t CacheStore::pendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    pruneExpiredLocked();

    std::string error;
    int64_t count = 0;
    const int64_t expire_after = common::nowMs() - ttl_ms_;
    if (!sqlite_->scalarInt64("SELECT COUNT(*) FROM telemetry_cache WHERE created_ts>=" +
                                  std::to_string(expire_after) + ";",
                              &count,
                              &error)) {
        logger_.error("CACHE", "failed to count SQLite telemetry cache: " + error);
        return 0;
    }
    return count > 0 ? static_cast<size_t>(count) : 0;
}

} // namespace gateway::storage
