#include "common/time_utils.h"

#include <chrono>
#include <ctime>

namespace gateway::common {

int64_t nowMs()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch())
        .count();
}

std::string localTimeText()
{
    const auto now = std::time(nullptr);
    std::tm local {};
    char buf[32] {};

    localtime_r(&now, &local);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

} // namespace gateway::common

