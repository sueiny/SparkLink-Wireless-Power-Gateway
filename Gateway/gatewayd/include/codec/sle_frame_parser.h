#pragma once

#include <cstdint>
#include <vector>

namespace gateway::codec {

// ── SLE 树网络帧常量（对齐 sle_gateway_protocol.md）──

constexpr uint8_t  SLE_FRAME_MAGIC_0     = 0x53;  // 'S'
constexpr uint8_t  SLE_FRAME_MAGIC_1     = 0x54;  // 'T'
constexpr uint8_t  SLE_FRAME_VERSION     = 0x01;
constexpr int      SLE_FRAME_HEADER_LEN  = 13;
constexpr int      SLE_FRAME_MAX_LEN     = 256;
constexpr int      SLE_FRAME_MAX_PAYLOAD = 243;   // 256 - 13

// 帧类型
constexpr uint8_t SLE_FRAME_TYPE_HEARTBEAT    = 1;
constexpr uint8_t SLE_FRAME_TYPE_DATA         = 2;
constexpr uint8_t SLE_FRAME_TYPE_TOPO_SUMMARY = 3;
constexpr uint8_t SLE_FRAME_TYPE_DEPTH_UPDATE = 4;

// 角色 ID
constexpr uint8_t SLE_ROLE_ROOT    = 1;
constexpr uint8_t SLE_ROLE_RELAY   = 2;
constexpr uint8_t SLE_ROLE_LEAF    = 3;
constexpr uint8_t SLE_ROLE_GATEWAY = 4;

// 固定节点 ID
constexpr uint16_t SLE_NODE_GATEWAY = 0x0000;
constexpr uint16_t SLE_NODE_ROOT_1  = 0x0001;
constexpr uint16_t SLE_NODE_ROOT_2  = 0x0002;
constexpr uint16_t SLE_NODE_ROOT_3  = 0x0003;

// ── 解析结果结构体 ──

// SLE 帧头解析结果（13 字节帧头）
struct SleFrameHeader {
    uint8_t  frame_type;
    uint8_t  src_role;
    uint16_t src_node_id;    // 小端
    uint16_t dst_node_id;    // 小端
    uint16_t seq;
    uint16_t payload_len;    // 小端
};

// DATA 帧 payload 解析结果
// payload 格式: modbus_type(1) + modbus_len(1) + modbus_rtu(N)
struct SleDataPayload {
    uint8_t  modbus_type;    // 2=电表, 3=温湿度, 4=继电器
    uint8_t  modbus_len;
    const uint8_t *modbus_rtu;  // 指向帧内数据，不拥有内存
};

// TOPO_SUMMARY 帧 payload 解析结果
struct SleTopoHeader {
    uint16_t node_id;
    uint8_t  child_count;
};

struct SleTopoChild {
    uint16_t node_id;
    uint8_t  role;
};

constexpr int SLE_TOPO_MAX_CHILDREN = 80;

// ── 解析函数 ──

// 解析帧头（13 字节）。成功返回 true。
bool parseSleFrameHeader(const uint8_t *raw, uint16_t raw_len, SleFrameHeader *header);

// 解析 DATA 帧 payload。
bool parseSleDataPayload(const uint8_t *payload, uint16_t payload_len, SleDataPayload *out);

// 解析 TOPO_SUMMARY 帧 payload。返回子节点数量，失败返回 -1。
int parseSleTopoPayload(const uint8_t *payload, uint16_t payload_len,
                        SleTopoHeader *header, SleTopoChild *children, int max_children);

} // namespace gateway::codec
