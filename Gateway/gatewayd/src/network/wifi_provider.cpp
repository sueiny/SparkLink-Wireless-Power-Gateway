#include "network/wifi_provider.h"

#include "network/network_utils.h"

#include <chrono>
#include <fstream>
#include <sys/stat.h>
#include <thread>

namespace gateway::network {
namespace {

std::string wpaConfigPath(const std::string &ifname)
{
    return "/tmp/gateway_wpa_" + ifname + ".conf";
}

bool writeWpaConfig(const std::string &ifname,
                    const std::string &ssid,
                    const std::string &password,
                    const std::string &country)
{
    const std::string path = wpaConfigPath(ifname);
    std::ofstream out(path, std::ios::trunc);
    if (!out)
        return false;
    out << "ctrl_interface=/var/run/wpa_supplicant\n"
        << "update_config=1\n"
        << "country=" << country << "\n"
        << "ap_scan=1\n"
        << "network={\n"
        << "    ssid=\"" << ssid << "\"\n"
        << "    scan_ssid=1\n"
        << "    psk=\"" << password << "\"\n"
        << "    key_mgmt=WPA-PSK\n"
        << "}\n";
    out.close();
    ::chmod(path.c_str(), 0600);
    return static_cast<bool>(out);
}

bool waitForWifiConnected(const std::string &ifname, int timeout_ms)
{
    int waited_ms = 0;
    while (waited_ms <= timeout_ms) {
        const auto status = runProcess({"wpa_cli", "-i", ifname, "status"}, 2000);
        if (status.stdout_output.find("wpa_state=COMPLETED") != std::string::npos)
            return true;
        const auto iw = runProcess({"iw", "dev", ifname, "link"}, 2000);
        if (iw.stdout_output.find("Connected") != std::string::npos)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        waited_ms += 500;
    }
    return false;
}

bool wifiAlreadyReady(const std::string &ifname, const std::string &ssid)
{
    if (!interfaceHasIpv4(ifname))
        return false;

    const auto status = runProcess({"wpa_cli", "-i", ifname, "status"}, 2000);
    if (!status.timeout && status.exit_code == 0 &&
        status.stdout_output.find("wpa_state=COMPLETED") != std::string::npos) {
        if (ssid.empty() || status.stdout_output.find("ssid=" + ssid) != std::string::npos)
            return true;
    }

    const auto iw = runProcess({"iw", "dev", ifname, "link"}, 2000);
    if (iw.timeout || iw.exit_code != 0)
        return false;
    if (iw.stdout_output.find("Connected") == std::string::npos)
        return false;
    if (!ssid.empty() && iw.stdout_output.find("SSID: " + ssid) == std::string::npos)
        return false;

    return true;
}

} // namespace

WifiProvider::WifiProvider(config::WifiConfig config, int priority)
    : config_(std::move(config)), priority_(priority)
{
}

bool WifiProvider::bringUp()
{
    if (config_.ssid.empty() || config_.password.empty())
        return false;

    if (wifiAlreadyReady(config_.ifname, config_.ssid))
        return true;

    if (!setInterfaceUp(config_.ifname))
        return false;
    if (!writeWpaConfig(config_.ifname, config_.ssid, config_.password, config_.country))
        return false;

    runProcess({"ip", "addr", "flush", "dev", config_.ifname}, 2000);
    runProcess({"wpa_cli", "-i", config_.ifname, "terminate"}, 2000);
    runProcess({"rm", "-f", "/var/run/wpa_supplicant/" + config_.ifname}, 2000);
    const auto start = runProcess({
        "wpa_supplicant",
        "-B",
        "-i",
        config_.ifname,
        "-c",
        wpaConfigPath(config_.ifname),
    }, 5000);
    if (start.timeout || start.exit_code != 0)
        return false;

    if (!waitForWifiConnected(config_.ifname, 15000))
        return false;
    return requestDhcp(config_.ifname, 15000);
}

bool WifiProvider::isInterfaceUp() const
{
    return defaultIsInterfaceUp(config_.ifname);
}

bool WifiProvider::hasIp() const
{
    return defaultHasIp(config_.ifname);
}

bool WifiProvider::canReachCloud(const std::string &host,
                                 int port,
                                 std::string *reason) const
{
    return defaultCanReachCloud(host, port, config_.ifname, reason);
}

} // namespace gateway::network
