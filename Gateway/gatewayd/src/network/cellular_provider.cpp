#include "network/cellular_provider.h"

#include "network/network_utils.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <initializer_list>
#include <poll.h>
#include <limits.h>
#include <sstream>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <utility>

namespace gateway::network {
namespace {

bool pathExists(const std::string &path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

std::string trimText(std::string text)
{
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' ||
            text.back() == ' ' || text.back() == '\t'))
        text.pop_back();
    size_t start = 0;
    while (start < text.size() &&
           (text[start] == '\n' || text[start] == '\r' ||
            text[start] == ' ' || text[start] == '\t'))
        ++start;
    return text.substr(start);
}

std::string readFirstLine(const std::string &path)
{
    std::string text;
    if (!readTextFile(path, &text))
        return {};
    const size_t nl = text.find('\n');
    if (nl != std::string::npos)
        text.resize(nl);
    return trimText(text);
}

std::string realPath(const std::string &path)
{
    char buffer[PATH_MAX] = {};
    if (::realpath(path.c_str(), buffer) == nullptr)
        return {};
    return buffer;
}

std::string basenameOfPath(const std::string &path)
{
    const size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool containsAny(const std::string &text, std::initializer_list<const char *> tokens)
{
    for (const char *token : tokens) {
        if (text.find(token) != std::string::npos)
            return true;
    }
    return false;
}

speed_t speedForBaudrate(int baudrate)
{
    switch (baudrate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
    default:
        return B115200;
    }
}

bool configureSerial(int fd, int baudrate)
{
    termios tio {};
    if (::tcgetattr(fd, &tio) != 0)
        return false;

    ::cfmakeraw(&tio);
    const speed_t speed = speedForBaudrate(baudrate);
    ::cfsetispeed(&tio, speed);
    ::cfsetospeed(&tio, speed);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
    tio.c_cflag &= static_cast<tcflag_t>(~PARENB);
    tio.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    tio.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 5;
    return ::tcsetattr(fd, TCSANOW, &tio) == 0;
}

std::string runAtCommand(const std::string &device,
                         int baudrate,
                         const std::string &command,
                         int timeout_ms)
{
    const int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return {};
    if (!configureSerial(fd, baudrate)) {
        ::close(fd);
        return {};
    }

    ::tcflush(fd, TCIOFLUSH);
    const std::string line = command + "\r\n";
    const ssize_t written = ::write(fd, line.data(), line.size());
    if (written < 0 || static_cast<size_t>(written) != line.size()) {
        ::close(fd);
        return {};
    }

    std::string response;
    const int step_ms = 100;
    int waited_ms = 0;
    while (waited_ms <= timeout_ms) {
        pollfd pfd {};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, step_ms);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            char buffer[256] = {};
            const ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                response.append(buffer, static_cast<size_t>(n));
                if (response.find("\r\nOK") != std::string::npos ||
                    response.find("\nOK") != std::string::npos ||
                    response.find("ERROR") != std::string::npos) {
                    break;
                }
            }
        } else if (pr < 0 && errno != EINTR) {
            break;
        }
        waited_ms += step_ms;
    }

    ::close(fd);
    return response;
}

bool responseHasOk(const std::string &response)
{
    return response.find("\r\nOK") != std::string::npos ||
           response.find("\nOK") != std::string::npos;
}

bool responseContainsAny(const std::string &response, std::initializer_list<const char *> tokens)
{
    for (const char *token : tokens) {
        if (response.find(token) != std::string::npos)
            return true;
    }
    return false;
}

std::vector<std::string> listSystemIfnames()
{
    std::vector<std::string> names;
    DIR *dir = ::opendir("/sys/class/net");
    if (!dir)
        return names;

    while (dirent *entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        names.push_back(name);
    }
    ::closedir(dir);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

CellularProvider::CellularProvider(config::CellularConfig config, int priority)
    : config_(std::move(config)), priority_(priority)
{
}

std::string CellularProvider::ifname() const
{
    return ensureStableIfname();
}

bool CellularProvider::bringUp(uint32_t route_metric)
{
    for (int attempt = 0; attempt < 10; ++attempt) {
        const std::string name = ifname();
        if (!name.empty()) {
            if (!pathExists(config_.serial_device))
                return false;
            if (!ensureHostDialup())
                return false;
            if (interfaceIsUp(name) && interfaceHasIpv4(name))
                return true;
            if (!setInterfaceUp(name))
                return false;
            return requestDhcp(name, 15000,
                               route_metric == 0 ? kCellularRouteMetric : route_metric);
        }
        ::usleep(500 * 1000);
    }

    return false;
}

bool CellularProvider::isInterfaceUp() const
{
    const std::string name = ifname();
    return !name.empty() && interfaceIsUp(name);
}

bool CellularProvider::hasIp() const
{
    const std::string name = ifname();
    return !name.empty() && interfaceHasIpv4(name);
}

bool CellularProvider::canReachCloud(const std::string &host,
                                     int port,
                                     std::string *reason) const
{
    std::string name = ifname();
    if (name.empty()) {
        if (reason)
            *reason = "ML307 ECM/RNDIS interface not found";
        return false;
    }
    return defaultCanReachCloud(host, port, name, reason);
}

std::string CellularProvider::ensureStableIfname() const
{
    const std::string desired = "cell0";
    if (!config_.ifname.empty() && config_.ifname != desired)
        return {};
    if (!isAllowedCellularIfname(desired))
        return {};
    if (interfaceExists(desired))
        return desired;

    const std::string ml307_ifname = findMl307Netdev();
    if (ml307_ifname.empty())
        return {};
    if (ml307_ifname == desired)
        return desired;

    setInterfaceDown(ml307_ifname);
    ProcessResult rename = runProcess({"ip", "link", "set", ml307_ifname, "name", desired}, 3000);
    if (rename.timeout || rename.exit_code != 0)
        return {};

    setInterfaceUp(desired);
    return interfaceExists(desired) ? desired : std::string{};
}

std::string CellularProvider::findMl307Netdev() const
{
    for (const auto &system_ifname : listSystemIfnames()) {
        if (system_ifname == "lo" || system_ifname == "eth1" ||
            system_ifname == "wlan0" || system_ifname == "usb0" ||
            system_ifname.compare(0, 3, "ppp") == 0 ||
            system_ifname.compare(0, 4, "wlan") == 0) {
            continue;
        }
        if (isMl307Netdev(system_ifname))
            return system_ifname;
    }
    return {};
}

bool CellularProvider::atControlReady() const
{
    if (!pathExists(config_.serial_device))
        return false;

    const std::string at = runAtCommand(config_.serial_device, config_.baudrate, "AT", 1500);
    if (!responseHasOk(at))
        return false;

    const std::string module = runAtCommand(config_.serial_device, config_.baudrate, "ATI", 1500);
    if (!responseHasOk(module) ||
        !responseContainsAny(module, {"ML307", "China Mobile", "CMIOT", "Mobile"})) {
        return false;
    }

    const std::string sim = runAtCommand(config_.serial_device, config_.baudrate, "AT+CPIN?", 1500);
    if (!responseHasOk(sim) || sim.find("READY") == std::string::npos)
        return false;

    for (const char *command : {"AT+CEREG?", "AT+CGREG?", "AT+CREG?"}) {
        const std::string registered =
            runAtCommand(config_.serial_device, config_.baudrate, command, 1500);
        if (responseHasOk(registered) && responseContainsAny(registered, {",1", ",5"}))
            return true;
    }
    return false;
}

bool CellularProvider::ensureHostDialup() const
{
    if (!atControlReady())
        return false;

    const std::string mipcall =
        runAtCommand(config_.serial_device, config_.baudrate, "AT+MIPCALL?", 2000);
    if (!responseHasOk(mipcall) || !responseContainsAny(mipcall, {"+MIPCALL: 1,1", "+MIPCALL:1,1"})) {
        const std::string start =
            runAtCommand(config_.serial_device, config_.baudrate, "AT+MIPCALL=1,1", 8000);
        if (!responseHasOk(start) && !responseContainsAny(start, {"+MIPCALL: 1,1", "+MIPCALL:1,1"}))
            return false;
    }

    const std::string auto_dial =
        runAtCommand(config_.serial_device, config_.baudrate, "AT+MDIALUPCFG=\"auto\",1", 2000);
    if (!responseHasOk(auto_dial) && auto_dial.find("ERROR") != std::string::npos)
        return false;

    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string status =
            runAtCommand(config_.serial_device, config_.baudrate, "AT+MDIALUP?", 3000);
        if (responseHasOk(status) &&
            responseContainsAny(status, {"+MDIALUP: 1,1", "+MDIALUP:1,1"})) {
            return true;
        }
        ::usleep(500 * 1000);
    }
    return false;
}

bool CellularProvider::isAllowedCellularIfname(const std::string &ifname) const
{
    return ifname == "cell0";
}

bool CellularProvider::isMl307Netdev(const std::string &ifname) const
{
    const std::string device_path = realPath("/sys/class/net/" + ifname + "/device");
    if (device_path.empty())
        return false;

    const std::string driver = basenameOfPath(realPath(device_path + "/driver"));
    const std::string modalias = readFirstLine(device_path + "/modalias");
    const std::string product = readFirstLine(device_path + "/../product");
    const std::string manufacturer = readFirstLine(device_path + "/../manufacturer");

    const bool known_driver = containsAny(driver, {"rndis_host", "cdc_ether", "cdc_ncm"});
    const bool known_device =
        modalias.find("v2ECCp3012") != std::string::npos ||
        product.find("ML307") != std::string::npos ||
        manufacturer.find("CMIOT") != std::string::npos;
    return known_driver && known_device;
}

} // namespace gateway::network
