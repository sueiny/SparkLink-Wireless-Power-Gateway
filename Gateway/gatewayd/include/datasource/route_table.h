#pragma once

#include "codec/sle_frame_parser.h"
#include "common/device_model.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace gateway::datasource {

struct RouteEntry {
    uint16_t node_id = 0;
    uint16_t parent_id = 0;                   // 父节点 ID，0=根节点
    uint8_t role = 0;                         // SLE 角色: 1=Root, 2=Relay, 3=Leaf
    std::vector<uint16_t> child_ids;          // 子节点 ID 列表
    bool online = true;
    int64_t last_update_ms = 0;
};

struct RouteUpdateResult {
    bool ok = false;
    bool complete = false;
    size_t observed_count = 0;
    size_t expected_count = 0;
    std::string error;
};

// Gateway 路由表。正式 root_report 模式只由 ST 0x05 快照覆盖更新。
class RouteTable {
public:
    RouteUpdateResult updateFromTopologyText(uint16_t report_root_id,
                                             const uint8_t *payload,
                                             uint16_t payload_len,
                                             size_t expected_count,
                                             const std::vector<model::DtuDeviceInfo> &inventory,
                                             int64_t now_ms);

    // 查询节点所属的 Root（沿 parent_id 向上查找）
    int findRootByNode(uint16_t node_id) const;

    // 节点是否在线：只看当前聚合 0x05 快照中的 online 标记。
    bool isOnline(uint16_t node_id) const;

    // 获取父节点 ID
    uint16_t getParentId(uint16_t node_id) const;

    // 获取子节点 ID 列表
    std::vector<uint16_t> getChildIds(uint16_t node_id) const;

    std::vector<RouteEntry> snapshot() const;
    void clear();

private:
    std::map<uint16_t, RouteEntry> aggregateSnapshotsLocked() const;

    mutable std::mutex mutex_;
    std::map<uint16_t, RouteEntry> entries_;
    std::map<uint16_t, std::map<uint16_t, RouteEntry>> root_snapshots_;
};

} // namespace gateway::datasource
