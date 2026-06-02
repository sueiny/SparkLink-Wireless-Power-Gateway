#pragma once

#include "common/device_model.h"

#include <cstdint>

namespace gateway::codec {

// Modbus RTU 解析器。
//
// 职责：校验 CRC16，按功能码解析寄存器/线圈值，
// 按 modbus_type 将原始值转为工程量，填充 TelemetryData。
//
// 支持的设备类型：
//   type=2 (单相电表): 功能码 0x04，整型寄存器 0x0190 起
//   type=3 (温湿度):   功能码 0x03，寄存器 0x0000-0x0001
//   type=4 (继电器):   功能码 0x01，线圈 0x0000-0x0003

// CRC16-Modbus 校验。返回 true 表示 CRC 正确。
bool verifyModbusCrc(const uint8_t *frame, uint16_t len);

// 解析 Modbus RTU 响应帧，输出填充好的 TelemetryData。
// rtu: 完整的 Modbus RTU 帧（含从站地址 + 功能码 + 数据 + CRC）。
// rtu_len: 帧长度。
// modbus_type: 设备类型（2=电表, 3=温湿度, 4=继电器）。
// out: 输出的 TelemetryData，device_id 和 type 由调用方设置。
// 返回 true 表示解析成功。
bool parseModbusResponse(const uint8_t *rtu, uint16_t rtu_len,
                         uint8_t modbus_type, model::TelemetryData &out);

} // namespace gateway::codec
