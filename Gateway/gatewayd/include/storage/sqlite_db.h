#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace gateway::storage {

struct SqliteRow {
    std::vector<std::string> columns;
};

// SQLite 的薄 RAII 封装。
// 上层 store 仍然保留业务语义，这里只提供打开、SQL 执行、事务和少量高频写入方法。
class SqliteDb {
public:
    SqliteDb();
    ~SqliteDb();

    SqliteDb(const SqliteDb &) = delete;
    SqliteDb &operator=(const SqliteDb &) = delete;

    bool open(const std::string &path, std::string *error);
    void close();
    bool isOpen() const;
    bool exec(const std::string &sql, std::string *error) const;
    bool query(const std::string &sql, std::vector<SqliteRow> *rows, std::string *error) const;
    bool scalarInt64(const std::string &sql, int64_t *value, std::string *error) const;

    // 以 BEGIN IMMEDIATE 包裹一段写操作；body 返回 false 时自动 ROLLBACK。
    bool withTransaction(const std::function<bool()> &body, std::string *error) const;

    // 使用 SQLite backup API 生成一致性备份，而不是直接复制正在写入的 db 文件。
    bool backupTo(const std::string &path, std::string *error) const;
    bool insertTelemetry(const std::string &topic,
                         const std::string &payload,
                         int64_t created_ts,
                         std::string *error) const;
    bool upsertDeviceState(const std::string &device_id,
                           const std::string &values_json,
                           int64_t updated_ts,
                           std::string *error) const;

private:
    sqlite3 *db_ = nullptr;
};

bool initializeGatewaySchema(SqliteDb &db, std::string *error);

} // namespace gateway::storage
