#include "network/network_utils.h"
#include "network/netlink_utils.h"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace gateway::network {

bool interfaceExists(const std::string &ifname)//检查接口是否存在
{
    struct stat st {};
    const std::string path = "/sys/class/net/" + ifname;// /sys/class/net/ 是Linux sysfs虚拟文件系统中的网络接口目录
    return ::stat(path.c_str(), &st) == 0;
}

bool interfaceIsUp(const std::string &ifname)
{
    std::ifstream in("/sys/class/net/" + ifname + "/operstate"); //operstate文件包含网络接口的当前状态，如up、down、unknown等
    std::string state;
    in >> state;
    return state == "up" || state == "unknown";
}

bool interfaceHasIpv4(const std::string &ifname)  //
{
    ifaddrs *addrs = nullptr;
    if (::getifaddrs(&addrs) != 0)
        return false;

    bool found = false;
    for (ifaddrs *it = addrs; it; it = it->ifa_next) {
        if (!it->ifa_addr || ifname != it->ifa_name)
            continue;
        if (it->ifa_addr->sa_family == AF_INET) {
            found = true;
            break;
        }
    }

    ::freeifaddrs(addrs);
    return found;
}

bool tcpConnect(const std::string &host, int port, int timeout_ms)
{
    if (host.empty() || port <= 0)
        return false;

    addrinfo hints {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    addrinfo *res = nullptr;
    const std::string port_text = std::to_string(port);

    // DNS 解析：dhcpcd 重写 resolv.conf 时 nameserver 会暂时消失。
    // 等待最多 15 秒（75×200ms），之后返回 false，由 ensureNetwork 下一轮重试。
    int gai_ret = EAI_AGAIN;
    for (int retry = 0; retry < 75 && gai_ret != 0; ++retry) {
        bool has_ns = false;
        {
            std::ifstream check("/etc/resolv.conf");
            std::string line;
            while (std::getline(check, line)) {
                if (line.find("nameserver") == 0) {
                    has_ns = true;
                    break;
                }
            }
        }
        if (!has_ns) {
            ::usleep(200000);
            continue;
        }
        gai_ret = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &res);
        if (gai_ret != 0)
            ::usleep(200000);
    }
    if (gai_ret != 0)
        return false;

    bool ok = false;
    for (addrinfo *it = res; it && !ok; it = it->ai_next) {
        const int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0)
            continue;

        const int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        const int rc = ::connect(fd, it->ai_addr, it->ai_addrlen);
        if (rc == 0) {
            ok = true;
        } else if (errno == EINPROGRESS) {
            pollfd pfd {};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            const int pr = ::poll(&pfd, 1, timeout_ms);
            if (pr > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0)
                    ok = true;
            }
        }

        ::close(fd);
    }

    ::freeaddrinfo(res);
    return ok;
}

bool tcpConnectVia(const std::string &host, int port, int timeout_ms, const std::string &ifname)
{
    // 已通过 Netlink 设置默认路由，直接用 tcpConnect 即可
    // SO_BINDTODEVICE 在某些内核版本会导致连接失败
    (void)ifname;
    return tcpConnect(host, port, timeout_ms);
}

bool setDefaultRouteVia(const std::string &ifname)
{
    // 使用 Netlink Socket 直接管理路由（不 fork 进程）
    return netlinkSetDefaultRouteVia(ifname);
}

ProcessResult runProcess(const std::vector<std::string> &args, int timeout_ms)
{
    ProcessResult result;
    if (args.empty())
        return result;

    int pipe_fd[2] = {-1, -1};
    // 使用 O_CLOEXEC 确保 pipe fd 不会被子进程继承
    if (::pipe2(pipe_fd, O_CLOEXEC) != 0)
        return result;

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return result;
    }

    if (pid == 0) {
        // 子进程：关闭所有非标准 fd，避免继承到 exec 的进程中
        // 这样 wpa_supplicant 等子进程不会持有 IPC socket 等 fd
        int max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0) max_fd = 1024;  // 保守上限
        for (int fd = 3; fd < max_fd; fd++) {
            if (fd != pipe_fd[1]) {
                ::close(fd);
            }
        }

        ::dup2(pipe_fd[1], STDOUT_FILENO);
        ::dup2(pipe_fd[1], STDERR_FILENO);
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args)
            argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    ::close(pipe_fd[1]);
    const int flags = ::fcntl(pipe_fd[0], F_GETFL, 0);
    ::fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);

    const int step_ms = 50;
    int waited_ms = 0;
    int status = 0;
    bool exited = false;

    while (waited_ms <= timeout_ms) {
        char buffer[256];
        while (true) {
            const ssize_t n = ::read(pipe_fd[0], buffer, sizeof(buffer));
            if (n > 0)
                result.stdout_output.append(buffer, static_cast<size_t>(n));
            else if (n < 0 && errno == EINTR)
                continue;
            else
                break;
        }

        const pid_t rc = ::waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            exited = true;
            break;
        }

        ::usleep(step_ms * 1000);
        waited_ms += step_ms;
    }

    if (!exited) {
        result.timeout = true;
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
    }

    char buffer[256];
    while (true) {
        const ssize_t n = ::read(pipe_fd[0], buffer, sizeof(buffer));
        if (n > 0)
            result.stdout_output.append(buffer, static_cast<size_t>(n));
        else if (n < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    ::close(pipe_fd[0]);

    if (exited && WIFEXITED(status))
        result.exit_code = WEXITSTATUS(status);
    return result;
}

bool setInterfaceUp(const std::string &ifname)
{
    if (ifname.empty())
        return false;

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;

    ifreq req {};
    std::snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ifname.c_str());
    bool ok = false;
    if (::ioctl(fd, SIOCGIFFLAGS, &req) == 0) {
        req.ifr_flags |= IFF_UP;
        ok = ::ioctl(fd, SIOCSIFFLAGS, &req) == 0;
    }
    ::close(fd);
    return ok;
}

bool requestDhcp(const std::string &ifname, int timeout_ms)
{
    if (ifname.empty())
        return false;

    if (interfaceHasIpv4(ifname))
        return true;

    const auto dhcpcd_pid = runProcess({"pidof", "dhcpcd"}, 1000);
    if (!dhcpcd_pid.timeout && dhcpcd_pid.exit_code == 0) {
        const auto metric = runProcess({"dhcpcd", "-m", "100", ifname}, timeout_ms);
        if (metric.timeout || metric.exit_code != 0)
            return false;
        const auto renew = runProcess({"dhcpcd", "-n", ifname}, timeout_ms);
        if (renew.timeout || renew.exit_code != 0)
            return false;
    } else {
        const auto result = runProcess({"udhcpc", "-i", ifname, "-n", "-q"}, timeout_ms);
        if (result.timeout || result.exit_code != 0)
            return false;
    }

    const int step_ms = 500;
    int waited_ms = 0;
    while (waited_ms <= timeout_ms) {
        if (interfaceHasIpv4(ifname))
            return true;
        ::usleep(step_ms * 1000);
        waited_ms += step_ms;
    }
    return false;
}

std::string firstUsableInterface(const std::vector<std::string> &ifnames)
{
    for (const auto &ifname : ifnames) {
        if (interfaceExists(ifname))
            return ifname;
    }
    return "";
}

} // namespace gateway::network
