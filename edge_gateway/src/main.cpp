#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <math.h>

namespace {

std::atomic_bool g_quit{false};

void handleSignal(int)
{
    g_quit.store(true);
}

void printHeartbeat()
{
    const auto now = std::time(nullptr);
    std::tm local {};
    char timeText[32] {};

    localtime_r(&now, &local);
    std::strftime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S", &local);

    std::cout << "[" << timeText << "] edge_gateway_demo heartbeat\n";
}

int parseInterval(int argc, char *argv[])
{
    if (argc <= 1)
        return 1;

    const int interval = std::atoi(argv[1]);
    return interval > 0 ? interval : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    const int intervalSec = parseInterval(argc, argv);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::cout << "edge_gateway_demo start, pid=" << getpid()
              << ", interval=" << intervalSec << "s\n";
    std::cout << "press Ctrl+C or send SIGTERM to stop\n";

    while (!g_quit.load()) {
        printHeartbeat();
        std::this_thread::sleep_for(std::chrono::seconds(intervalSec));
    }

    std::cout << "edge_gateway_demo exit\n";
    return 0;
}
