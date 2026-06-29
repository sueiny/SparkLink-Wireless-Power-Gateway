#pragma once

#include "common/device_model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gateway::codec {

struct SleDownlinkFrame {
    uint16_t root_node_id = 0;
    uint16_t dst_node_id = 0;     // ST frame destination DTU/node.
    uint16_t target_node_id = 0;  // Business target DTU/node for diagnostics.
    std::vector<uint8_t> frame;
};

// 构造 gatewayd -> sle_data_app -> DTU 的完整 ST DATA 下行帧。
// gatewayd 持有物模型和 Modbus 语义；sle_data_app 只转发 raw ST。
// IPC meta 里的 root_node_id 用于选择 root 连接；ST 帧头 dst_node_id 指向目标 DTU。
bool buildSetRelayDownlinkFrame(const model::DeviceInfo &device,
                                uint16_t root_node_id,
                                int state,
                                uint8_t relay_channel,
                                uint16_t seq,
                                SleDownlinkFrame *out,
                                std::string *error);

std::string hexText(const uint8_t *data, size_t len);

} // namespace gateway::codec
