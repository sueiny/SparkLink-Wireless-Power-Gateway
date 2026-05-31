#include "common/logger.h"

#include "common/file_utils.h"
#include "common/time_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

namespace gateway::log {
namespace {

constexpr long kMaxLogBytes = 5L * 1024L * 1024L;
constexpr int kMaxRotateFiles = 3;

const char *levelText(Level level)
{
    switch (level) {
    case Level::Debug:
        return "DEBUG";
    case Level::Warn:
        return "WARN";
    case Level::Error:
        return "ERROR";
    case Level::Info:
    default:
        return "INFO";
    }
}

Level levelFromText(const std::string &text)
{
    std::string normalized = text;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "debug")
        return Level::Debug;
    if (normalized == "warn" || normalized == "warning")
        return Level::Warn;
    if (normalized == "error")
        return Level::Error;
    return Level::Info;
}

bool fileTooLarge(const std::string &path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && st.st_size >= kMaxLogBytes;
}

} // namespace

bool Logger::init(const std::string &log_dir)
{
    return init(log_dir, "info");
}

bool Logger::init(const std::string &log_dir, const std::string &level_text)
{
    if (!common::mkdirRecursive(log_dir))
        return false;

    min_level_ = levelFromText(level_text);
    log_path_ = log_dir + "/gateway.log";
    rotateIfNeeded();
    if (file_.is_open())
        file_.close();
    file_.clear();
    file_.open(log_path_, std::ios::app);
    return file_.is_open();
}

void Logger::setLevel(Level level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

bool Logger::isLevelEnabled(Level level) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(level) >= static_cast<int>(min_level_);
}

void Logger::debug(const std::string &module, const std::string &message)
{
    write(Level::Debug, module, message);
}

void Logger::info(const std::string &module, const std::string &message)
{
    write(Level::Info, module, message);
}

void Logger::warn(const std::string &module, const std::string &message)
{
    write(Level::Warn, module, message);
}

void Logger::error(const std::string &module, const std::string &message)
{
    write(Level::Error, module, message);
}

void Logger::write(Level level, const std::string &module, const std::string &message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(min_level_))
        return;

    std::ostringstream line;
    line << "[" << common::localTimeText() << "]"
         << "[" << levelText(level) << "]"
         << "[" << module << "] " << message;

    if (file_.is_open()) {
        rotateIfNeeded();
        file_ << line.str() << '\n';
        file_.flush();
    } else {
        std::cerr << line.str() << '\n';
    }
}

void Logger::rotateIfNeeded()
{
    if (log_path_.empty() || !fileTooLarge(log_path_))
        return;

    if (file_.is_open())
        file_.close();

    const std::string oldest = log_path_ + "." + std::to_string(kMaxRotateFiles);
    std::remove(oldest.c_str());
    for (int index = kMaxRotateFiles - 1; index >= 1; --index) {
        const std::string from = log_path_ + "." + std::to_string(index);
        const std::string to = log_path_ + "." + std::to_string(index + 1);
        std::rename(from.c_str(), to.c_str());
    }
    std::rename(log_path_.c_str(), (log_path_ + ".1").c_str());
    file_.open(log_path_, std::ios::app);
}

} // namespace gateway::log
