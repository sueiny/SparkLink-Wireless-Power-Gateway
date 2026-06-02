#pragma once

#include "codec/sle_frame_parser.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace gateway::datasource {

struct RouteEntry {
    uint16_t node_id = 0;
    uint16_t parent_id = 0;                   // 父节点 ID，0=根节点
    uint8_t role = 0;                         // SLE 角色: 1=Root, 2=Relay, 3=Leaf
    std::vector<uint16_t> child_ids;          // 子节点 ID 列表
    int64_t last_update_ms = 0;
};

// Gateway 路由表。从 TOPO_SUMMARY 帧动态构建。
// V2：ID-based，支持 parent_id/child_ids 查询。
class RouteTable {
public:
    void updateFromTopo(uint16_t root_node_id, const uint8_t *payload, uint16_t payload_len,
                        int64_t now_ms);

    // 查询节点所属的 Root（沿 parent_id 向上查找）
    int findRootByNode(uint16_t node_id) const;

    // 节点是否在线（路由表中存在即在线）
    bool isOnline(uint16_t node_id) const;

    // 获取父节点 ID
    uint16_t getParentId(uint16_t node_id) const;

    // 获取子节点 ID 列表
    std::vector<uint16_t> getChildIds(uint16_t node_id) const;

    void markExpired(int64_t now_ms, uint32_t timeout_ms);
    std::vector<RouteEntry> snapshot() const;
    void clear();

private:
    mutable std::mutex mutex_;
    std::map<uint16_t, RouteEntry> entries_;
};

} // namespace gateway::datasource
