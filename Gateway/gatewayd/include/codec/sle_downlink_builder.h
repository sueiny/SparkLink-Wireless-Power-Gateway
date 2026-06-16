#pragma once

#include "common/device_model.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gateway::codec {

struct SleDownlinkFrame {
    uint16_t root_node_id = 0;
    uint16_t dst_node_id = 0;     // ST frame transport destination: root node.
    uint16_t target_node_id = 0;  // Business target DTU/node for diagnostics only.
    std::vector<uint8_t> frame;
};

// 构造 gatewayd -> sle_data_app -> DTU 的完整 ST DATA 下行帧。
// gatewayd 持有物模型和 Modbus 语义；sle_data_app 只转发 raw ST。
// 下行 ST 帧头 dst_node_id 固定指向 Root，Root 内部如何树状转发不由 gatewayd 处理。
bool buildSetRelayDownlinkFrame(const model::DeviceInfo &device,
                                uint16_t root_node_id,
                                int state,
                                uint8_t relay_channel,
                                uint16_t seq,
                                SleDownlinkFrame *out,
                                std::string *error);

std::string hexText(const uint8_t *data, size_t len);

} // namespace gateway::codec
