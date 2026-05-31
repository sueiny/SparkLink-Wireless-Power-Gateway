#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace gateway::log {

enum class Level {
    Debug,
    Info,
    Warn,
    Error,
};

class Logger {
public:
    bool init(const std::string &log_dir);
    bool init(const std::string &log_dir, const std::string &level_text);
    void setLevel(Level level);
    bool isLevelEnabled(Level level) const;
    void debug(const std::string &module, const std::string &message);
    void info(const std::string &module, const std::string &message);
    void warn(const std::string &module, const std::string &message);
    void error(const std::string &module, const std::string &message);

private:
    void rotateIfNeeded();
    void write(Level level, const std::string &module, const std::string &message);

    mutable std::mutex mutex_;
    std::ofstream file_;
    std::string log_path_;
    Level min_level_ = Level::Info;
};

} // namespace gateway::log
