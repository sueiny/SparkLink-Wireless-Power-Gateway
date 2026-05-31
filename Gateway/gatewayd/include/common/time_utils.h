#pragma once

#include <cstdint>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

namespace gateway::common {

int64_t nowMs();
std::string localTimeText();

inline bool interruptibleSleep(const std::atomic_bool &stop,
                               int total_ms,
                               int step_ms = 100)
{
    int slept_ms = 0;
    while (!stop.load() && slept_ms < total_ms) {
        const int sleep_ms = std::min(step_ms, total_ms - slept_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        slept_ms += sleep_ms;
    }
    return stop.load();
}

} // namespace gateway::common
