#include "storage/sqlite_db.h"

#include "common/file_utils.h"

#include <sqlite3.h>

#include <exception>
#include <mutex>

namespace gateway::storage {
namespace {

std::mutex g_backup_mutex;

} // namespace

SqliteDb::SqliteDb() = default;

SqliteDb::~SqliteDb()
{
    close();
}

bool SqliteDb::open(const std::string &path, std::string *error)
{
    close();
    if (!common::ensureParentDir(path)) {
        if (error)
            *error = "failed to create sqlite parent dir";
        return false;
    }

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        if (error)
            *error = db_ ? sqlite3_errmsg(db_) : "sqlite3_open_v2 failed";
        close();
        return false;
    }

    return exec("PRAGMA journal_mode=WAL;"
                "PRAGMA synchronous=NORMAL;"
                "PRAGMA busy_timeout=3000;",
                error);
}

void SqliteDb::close()
{
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SqliteDb::isOpen() const
{
    return db_ != nullptr;
}

bool SqliteDb::exec(const std::string &sql, std::string *error) const
{
    char *errmsg = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc == SQLITE_OK)
        return true;

    if (error)
        *error = errmsg ? errmsg : sqlite3_errmsg(db_);
    sqlite3_free(errmsg);
    return false;
}

bool SqliteDb::query(const std::string &sql, std::vector<SqliteRow> *rows, std::string *error) const
{
    if (!rows)
        return false;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if (error)
            *error = sqlite3_errmsg(db_);
        return false;
    }

    rows->clear();
    while (true) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE)
            break;
        if (rc != SQLITE_ROW) {
            if (error)
                *error = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            return false;
        }

        SqliteRow row;
        const int count = sqlite3_column_count(stmt);
        row.columns.reserve(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index) {
            const auto *text = sqlite3_column_text(stmt, index);
            row.columns.emplace_back(text ? reinterpret_cast<const char *>(text) : "");
        }
        rows->push_back(std::move(row));
    }

    sqlite3_finalize(stmt);
    return true;
}

bool SqliteDb::scalarInt64(const std::string &sql, int64_t *value, std::string *error) const
{
    std::vector<SqliteRow> rows;
    if (!query(sql, &rows, error) || rows.empty() || rows[0].columns.empty())
        return false;
    try {
        if (value)
            *value = std::stoll(rows[0].columns[0]);
    } catch (const std::exception &e) {
        if (error)
            *error = std::string("failed to parse sqlite scalar: ") + e.what();
        return false;
    }
    return true;
}

bool SqliteDb::withTransaction(const std::function<bool()> &body, std::string *error) const
{
    if (!exec("BEGIN IMMEDIATE;", error))
        return false;

    if (!body()) {
        std::string rollback_error;
        exec("ROLLBACK;", &rollback_error);
        return false;
    }

    if (!exec("COMMIT;", error)) {
        std::string rollback_error;
        exec("ROLLBACK;", &rollback_error);
        return false;
    }
    return true;
}

bool SqliteDb::backupTo(const std::string &path, std::string *error) const
{
    std::lock_guard<std::mutex> lock(g_backup_mutex);

    if (!db_) {
        if (error)
            *error = "SQLite database is not open";
        return false;
    }
    if (!common::ensureParentDir(path)) {
        if (error)
            *error = "failed to create sqlite backup parent dir";
        return false;
    }

    common::unlinkIfExists(path);

    sqlite3 *dest = nullptr;
    if (sqlite3_open_v2(path.c_str(), &dest, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        if (error)
            *error = dest ? sqlite3_errmsg(dest) : "sqlite3_open_v2 backup failed";
        if (dest)
            sqlite3_close(dest);
        return false;
    }

    sqlite3_backup *backup = sqlite3_backup_init(dest, "main", db_, "main");
    if (!backup) {
        if (error)
            *error = sqlite3_errmsg(dest);
        sqlite3_close(dest);
        return false;
    }

    const int step_rc = sqlite3_backup_step(backup, -1);
    const int finish_rc = sqlite3_backup_finish(backup);
    if (step_rc != SQLITE_DONE || finish_rc != SQLITE_OK) {
        if (error)
            *error = sqlite3_errmsg(dest);
        sqlite3_close(dest);
        return false;
    }

    sqlite3_close(dest);
    return true;
}

bool SqliteDb::insertTelemetry(const std::string &topic,
                               const std::string &payload,
                               int64_t created_ts,
                               std::string *error) const
{
    sqlite3_stmt *stmt = nullptr;
    constexpr const char *kSql =
        "INSERT INTO telemetry_cache(topic,payload,created_ts) VALUES(?,?,?);";
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error)
            *error = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, created_ts);
    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && error)
        *error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool SqliteDb::upsertDeviceState(const std::string &device_id,
                                 const std::string &values_json,
                           int64_t updated_ts,
                           std::string *error) const
{
    sqlite3_stmt *stmt = nullptr;
    constexpr const char *kSql =
        "INSERT INTO device_state(device_id,values_json,updated_ts) VALUES(?,?,?) "
        "ON CONFLICT(device_id) DO UPDATE SET "
        "values_json=excluded.values_json,updated_ts=excluded.updated_ts;";
    if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
        if (error)
            *error = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, values_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, updated_ts);
    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && error)
        *error = sqlite3_errmsg(db_);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool initializeGatewaySchema(SqliteDb &db, std::string *error)
{
    return db.exec(
        "CREATE TABLE IF NOT EXISTS telemetry_cache ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "topic TEXT NOT NULL,"
        "payload TEXT NOT NULL,"
        "created_ts INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS device_state ("
        "device_id TEXT PRIMARY KEY,"
        "values_json TEXT NOT NULL,"
        "updated_ts INTEGER NOT NULL"
        ");",
        error);
}

} // namespace gateway::storage
