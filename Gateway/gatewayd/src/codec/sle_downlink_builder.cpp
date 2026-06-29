#include "codec/sle_downlink_builder.h"

#include "codec/sle_frame_parser.h"

#include <cstdio>
#include <sstream>

namespace gateway::codec {
namespace {

uint16_t crc16Modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1)
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            else
                crc >>= 1;
        }
    }
    return crc;
}

uint8_t modbusAddrFor(const model::DeviceInfo &device)
{
    if (device.modbus_addr > 0)
        return static_cast<uint8_t>(device.modbus_addr);
    if (device.station_id > 0)
        return static_cast<uint8_t>(device.station_id);
    return 0;
}

std::vector<uint8_t> buildMeterRtu(const model::DeviceInfo &device, int state, std::string *error)
{
    const uint8_t addr = modbusAddrFor(device);
    if (addr == 0) {
        if (error)
            *error = "meter modbus address missing";
        return {};
    }

    const uint16_t value = state == 0 ? 0xAAAA : 0x5555;
    std::vector<uint8_t> frame = {
        addr,
        0x10,
        0x00, 0x10,
        0x00, 0x01,
        0x02,
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF),
    };
    const uint16_t crc = crc16Modbus(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

std::vector<uint8_t> buildRelayRtu(const model::DeviceInfo &device,
                                   int state,
                                   uint8_t relay_channel,
                                   std::string *error)
{
    const uint8_t addr = modbusAddrFor(device);
    if (addr == 0) {
        if (error)
            *error = "relay modbus address missing";
        return {};
    }

    const uint16_t value = state == 0 ? 0x0000 : 0xFF00;
    std::vector<uint8_t> frame = {
        addr,
        0x05,
        0x00, relay_channel,
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF),
    };
    const uint16_t crc = crc16Modbus(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    return frame;
}

void appendU16LE(std::vector<uint8_t> *out, uint16_t value)
{
    out->push_back(static_cast<uint8_t>(value & 0xFF));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

bool buildStFrame(uint16_t root_node_id,
                  uint16_t dst_node_id,
                  uint16_t seq,
                  const std::vector<uint8_t> &payload,
                  SleDownlinkFrame *out,
                  std::string *error)
{
    if (payload.size() > SLE_FRAME_MAX_PAYLOAD) {
        if (error)
            *error = "payload too large for ST frame";
        return false;
    }

    if (!out)
        return false;

    out->root_node_id = root_node_id;
    out->dst_node_id = dst_node_id;
    out->frame.clear();
    out->frame.reserve(SLE_FRAME_HEADER_LEN + payload.size());
    out->frame.push_back(SLE_FRAME_MAGIC_0);
    out->frame.push_back(SLE_FRAME_MAGIC_1);
    out->frame.push_back(SLE_FRAME_VERSION);
    out->frame.push_back(SLE_FRAME_TYPE_DATA);
    out->frame.push_back(SLE_ROLE_GATEWAY);
    appendU16LE(&out->frame, 0);
    appendU16LE(&out->frame, dst_node_id);
    appendU16LE(&out->frame, seq);
    appendU16LE(&out->frame, static_cast<uint16_t>(payload.size()));
    out->frame.insert(out->frame.end(), payload.begin(), payload.end());
    return true;
}

} // namespace

bool buildSetRelayDownlinkFrame(const model::DeviceInfo &device,
                                uint16_t root_node_id,
                                int state,
                                uint8_t relay_channel,
                                uint16_t seq,
                                SleDownlinkFrame *out,
                                std::string *error)
{
    if (device.type != model::DeviceType::SinglePhaseMeter &&
        device.type != model::DeviceType::Relay) {
        if (error)
            *error = "unsupported device type for set_relay";
        return false;
    }

    if (state != 0 && state != 1) {
        if (error)
            *error = "relay state must be 0 or 1";
        return false;
    }

    std::vector<uint8_t> rtu;
    if (device.type == model::DeviceType::SinglePhaseMeter) {
        rtu = buildMeterRtu(device, state, error);
    } else {
        rtu = buildRelayRtu(device, state, relay_channel, error);
    }
    if (rtu.empty())
        return false;

    const uint16_t target_node_id = static_cast<uint16_t>(device.dtu_id);
    if (target_node_id == 0) {
        if (error)
            *error = "target DTU id missing";
        return false;
    }

    // root_node_id selects the SLE root connection; ST dst_node_id targets the
    // DTU currently mounting the external device. DATA payload is pure RTU.
    const bool ok = buildStFrame(root_node_id, target_node_id, seq, rtu, out, error);
    if (ok && out)
        out->target_node_id = target_node_id;
    return ok;
}

std::string hexText(const uint8_t *data, size_t len)
{
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        if (i > 0)
            oss << ' ';
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02X", data[i]);
        oss << buf;
    }
    return oss.str();
}

} // namespace gateway::codec
