#include "codec/modbus_parser.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace gateway::codec {

// Modbus 设备类型常量（对齐 Modbus 寄存器仿真规格.md）
static constexpr uint8_t kModbusTypeMeter = 2;
static constexpr uint8_t kModbusTypeEnv   = 3;
static constexpr uint8_t kModbusTypeRelay = 4;

// 电表整型寄存器缩放因子
static constexpr double kMeterVoltageScale     = 0.1;   // ×0.1 V
static constexpr double kMeterCurrentScale     = 0.01;  // ×0.01 A
static constexpr double kMeterPowerFactorScale = 0.001; // ×0.001
static constexpr double kMeterFrequencyScale   = 0.01;  // ×0.01 Hz
static constexpr double kMeterEnergyScale      = 0.01;  // ×0.01 kWh

// 温湿度缩放因子
static constexpr double kEnvScale = 0.1; // ×0.1

// 大端读取 uint16_t
static uint16_t readU16BE(const uint8_t *p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

// 大端读取 int16_t
static int16_t readI16BE(const uint8_t *p)
{
    return static_cast<int16_t>((p[0] << 8) | p[1]);
}

// 大端读取 uint32_t（高字节在前）
static uint32_t readU32BE(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// 四舍五入到小数点后 2 位
static double round2(double value)
{
    return std::round(value * 100.0) / 100.0;
}

bool verifyModbusCrc(const uint8_t *frame, uint16_t len)
{
    if (frame == nullptr || len < 4) {
        return false;
    }

    // 计算 CRC16（除最后 2 字节 CRC 外）
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len - 2; ++i) {
        crc ^= frame[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }

    // 帧尾 CRC 低字节在前
    uint16_t frame_crc = static_cast<uint16_t>(frame[len - 2] | (frame[len - 1] << 8));
    return crc == frame_crc;
}

// 解析电表整型寄存器（功能码 0x04，从 0x0190 起）
// 寄存器布局：V(1) I(1) P(1) PF(1) F(1) E_hi(1) E_lo(1) relay(1) = 8 个寄存器
// 返回值：需要的寄存器数（8），数据字节数 = 16
static bool parseMeterRegisters(const uint8_t *data, uint8_t byte_count, model::TelemetryData &out)
{
    // 至少需要 16 字节（8 个寄存器 × 2 字节）
    if (byte_count < 16) {
        fprintf(stderr, "[MODBUS] meter: byte_count %u < 16\n", byte_count);
        return false;
    }

    uint16_t raw_voltage     = readU16BE(data + 0);
    uint16_t raw_current     = readU16BE(data + 2);
    uint16_t raw_power       = readU16BE(data + 4);
    uint16_t raw_pf          = readU16BE(data + 6);
    uint16_t raw_freq        = readU16BE(data + 8);
    uint32_t raw_energy      = readU32BE(data + 10);
    uint16_t raw_relay       = readU16BE(data + 14);

    out.numeric_values["voltage"]      = round2(raw_voltage * kMeterVoltageScale);
    out.numeric_values["current"]      = round2(raw_current * kMeterCurrentScale);
    out.numeric_values["active_power"] = round2(raw_power); // 已经是 W
    out.numeric_values["power_factor"] = round2(raw_pf * kMeterPowerFactorScale);
    out.numeric_values["frequency"]    = round2(raw_freq * kMeterFrequencyScale);
    out.numeric_values["energy"]       = round2(raw_energy * kMeterEnergyScale);
    out.integer_values["relay_status"] = (raw_relay == 0x55) ? 1 : 0;

    // 物模型兼容字段（支表填 0）
    out.numeric_values["branch_power_sum"] = 0.0;
    out.numeric_values["power_loss"]       = 0.0;
    out.numeric_values["loss_rate"]        = 0.0;

    return true;
}

// 解析温湿度寄存器（功能码 0x03，从 0x0000 起）
// 寄存器布局：humidity(1) temperature(1) = 2 个寄存器
static bool parseEnvRegisters(const uint8_t *data, uint8_t byte_count, model::TelemetryData &out)
{
    if (byte_count < 4) {
        fprintf(stderr, "[MODBUS] env: byte_count %u < 4\n", byte_count);
        return false;
    }

    uint16_t raw_humidity    = readU16BE(data + 0);
    int16_t  raw_temperature = readI16BE(data + 2);

    out.numeric_values["humidity"]    = round2(raw_humidity * kEnvScale);
    out.numeric_values["temperature"] = round2(raw_temperature * kEnvScale);

    return true;
}

// 解析继电器线圈状态（功能码 0x01）
// 线圈布局：relay_1 ~ relay_4，byte_count 个字节，每字节 8 个线圈
static bool parseRelayCoils(const uint8_t *data, uint8_t byte_count, model::TelemetryData &out)
{
    if (byte_count < 1) {
        fprintf(stderr, "[MODBUS] relay: byte_count %u < 1\n", byte_count);
        return false;
    }

    // 只取第 1 路继电器（bit 0）
    out.integer_values["relay_state"]  = (data[0] & 0x01) ? 1 : 0;
    out.integer_values["control_mode"] = 0; // 本地模式

    return true;
}

bool parseModbusResponse(const uint8_t *rtu, uint16_t rtu_len,
                         uint8_t modbus_type, model::TelemetryData &out)
{
    if (rtu == nullptr || rtu_len < 4) {
        fprintf(stderr, "[MODBUS] frame too short: %u\n", rtu_len);
        return false;
    }

    // 校验 CRC
    if (!verifyModbusCrc(rtu, rtu_len)) {
        fprintf(stderr, "[MODBUS] CRC error\n");
        return false;
    }

    /* rtu[0] = slave_addr, 用于后续按从站地址区分设备 */
    uint8_t func_code  = rtu[1];

    // 检查是否为异常响应（功能码最高位为 1）
    if (func_code & 0x80) {
        fprintf(stderr, "[MODBUS] exception response: func=0x%02x\n", func_code);
        return false;
    }

    // 根据功能码解析数据
    if (func_code == 0x03 || func_code == 0x04) {
        // 读保持/输入寄存器响应: addr(1) + func(1) + byte_count(1) + data(N) + crc(2)
        if (rtu_len < 5) {
            return false;
        }
        uint8_t byte_count = rtu[2];
        if (3 + byte_count + 2 > rtu_len) {
            fprintf(stderr, "[MODBUS] byte_count %u overflows frame\n", byte_count);
            return false;
        }
        const uint8_t *data = rtu + 3;

        switch (modbus_type) {
        case kModbusTypeMeter:
            return parseMeterRegisters(data, byte_count, out);
        case kModbusTypeEnv:
            return parseEnvRegisters(data, byte_count, out);
        default:
            fprintf(stderr, "[MODBUS] unknown modbus_type %u for func 0x%02x\n",
                    modbus_type, func_code);
            return false;
        }
    } else if (func_code == 0x01) {
        // 读线圈状态响应: addr(1) + func(1) + byte_count(1) + data(N) + crc(2)
        if (rtu_len < 5) {
            return false;
        }
        uint8_t byte_count = rtu[2];
        if (3 + byte_count + 2 > rtu_len) {
            return false;
        }

        if (modbus_type == kModbusTypeRelay) {
            return parseRelayCoils(rtu + 3, byte_count, out);
        }
        fprintf(stderr, "[MODBUS] unknown modbus_type %u for func 0x01\n", modbus_type);
        return false;
    }

    fprintf(stderr, "[MODBUS] unsupported func_code 0x%02x\n", func_code);
    return false;
}

} // namespace gateway::codec
