#include "codec/sle_frame_parser.h"

#include <cstdio>
#include <cstring>

namespace gateway::codec {

// 小端读取 uint16_t
static uint16_t readU16LE(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

bool parseSleFrameHeader(const uint8_t *raw, uint16_t raw_len, SleFrameHeader *header)
{
    if (raw == nullptr || header == nullptr) {
        return false;
    }
    if (raw_len < SLE_FRAME_HEADER_LEN) {
        fprintf(stderr, "[SLE-FRAME] too short: %u < %d\n", raw_len, SLE_FRAME_HEADER_LEN);
        return false;
    }

    // 校验 magic
    if (raw[0] != SLE_FRAME_MAGIC_0 || raw[1] != SLE_FRAME_MAGIC_1) {
        fprintf(stderr, "[SLE-FRAME] bad magic: 0x%02x 0x%02x\n", raw[0], raw[1]);
        return false;
    }

    // 校验 version
    if (raw[2] != SLE_FRAME_VERSION) {
        fprintf(stderr, "[SLE-FRAME] bad version: 0x%02x\n", raw[2]);
        return false;
    }

    header->frame_type  = raw[3];
    header->src_role    = raw[4];
    header->src_node_id = readU16LE(raw + 5);
    header->dst_node_id = readU16LE(raw + 7);
    header->seq         = readU16LE(raw + 9);
    header->payload_len = readU16LE(raw + 11);

    // 校验 payload_len
    if (header->payload_len > raw_len - SLE_FRAME_HEADER_LEN) {
        fprintf(stderr, "[SLE-FRAME] payload_len %u > remaining %u\n",
                header->payload_len, raw_len - SLE_FRAME_HEADER_LEN);
        return false;
    }

    return true;
}

bool parseSleDataPayload(const uint8_t *payload, uint16_t payload_len, SleDataPayload *out)
{
    if (payload == nullptr || out == nullptr) {
        return false;
    }
    // 最小: modbus_type(1) + modbus_len(1) = 2 字节
    if (payload_len < 2) {
        fprintf(stderr, "[SLE-DATA] payload too short: %u\n", payload_len);
        return false;
    }

    out->modbus_type = payload[0];
    out->modbus_len  = payload[1];

    if (out->modbus_len == 0) {
        fprintf(stderr, "[SLE-DATA] modbus_len is 0\n");
        return false;
    }
    if (out->modbus_len > payload_len - 2) {
        fprintf(stderr, "[SLE-DATA] modbus_len %u > remaining %u\n",
                out->modbus_len, payload_len - 2);
        return false;
    }

    out->modbus_rtu = payload + 2;
    return true;
}

int parseSleTopoPayload(const uint8_t *payload, uint16_t payload_len,
                        SleTopoHeader *header, SleTopoChild *children, int max_children)
{
    if (payload == nullptr || header == nullptr) {
        return -1;
    }
    // 最小: node_id(2) + child_count(1) = 3 字节
    if (payload_len < 3) {
        return -1;
    }

    header->node_id     = readU16LE(payload);
    header->child_count = payload[2];

    int count = 0;
    if (children != nullptr && max_children > 0) {
        // 每个子节点: node_id(2) + role(1) = 3 字节
        int available = (payload_len - 3) / 3;
        count = header->child_count < available ? header->child_count : available;
        if (count > max_children) {
            count = max_children;
        }

        for (int i = 0; i < count; ++i) {
            const uint8_t *p = payload + 3 + i * 3;
            children[i].node_id = readU16LE(p);
            children[i].role    = p[2];
        }
    }

    return count;
}

} // namespace gateway::codec
