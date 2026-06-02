#include "datasource/route_table.h"
#include "common/logger.h"

#include <cstdio>

namespace gateway::datasource {

void RouteTable::updateFromTopo(uint16_t root_node_id, const uint8_t *payload,
                                uint16_t payload_len, int64_t now_ms)
{
    codec::SleTopoHeader header;
    codec::SleTopoChild children[codec::SLE_TOPO_MAX_CHILDREN];

    int count = codec::parseSleTopoPayload(payload, payload_len, &header,
                                           children, codec::SLE_TOPO_MAX_CHILDREN);
    if (count < 0)
        return;

    std::lock_guard<std::mutex> lock(mutex_);

    // 先删除该 Root 之前的旧条目
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        if (it->second.parent_id == root_node_id ||
            it->second.node_id == root_node_id) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }

    // 写入 Root 自身
    RouteEntry root_entry;
    root_entry.node_id = header.node_id;
    root_entry.parent_id = 0;
    root_entry.role = codec::SLE_ROLE_ROOT;
    root_entry.last_update_ms = now_ms;
    for (int i = 0; i < count; ++i)
        root_entry.child_ids.push_back(children[i].node_id);
    entries_[header.node_id] = root_entry;

    // 写入子节点
    for (int i = 0; i < count; ++i) {
        RouteEntry entry;
        entry.node_id = children[i].node_id;
        entry.parent_id = header.node_id;
        entry.role = children[i].role;
        entry.last_update_ms = now_ms;
        entries_[entry.node_id] = entry;
    }
}

int RouteTable::findRootByNode(uint16_t node_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(node_id);
    if (it == entries_.end())
        return -1;
    // 沿 parent_id 向上查找 Root
    uint16_t current = node_id;
    for (int depth = 0; depth < 10; ++depth) {
        auto jt = entries_.find(current);
        if (jt == entries_.end())
            return -1;
        if (jt->second.parent_id == 0)
            return static_cast<int>(current);
        current = jt->second.parent_id;
    }
    return -1;
}

bool RouteTable::isOnline(uint16_t node_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.count(node_id) > 0;
}

uint16_t RouteTable::getParentId(uint16_t node_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.parent_id : 0;
}

std::vector<uint16_t> RouteTable::getChildIds(uint16_t node_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(node_id);
    return (it != entries_.end()) ? it->second.child_ids : std::vector<uint16_t>{};
}

void RouteTable::markExpired(int64_t now_ms, uint32_t timeout_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        if (now_ms - it->second.last_update_ms > static_cast<int64_t>(timeout_ms)) {
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<RouteEntry> RouteTable::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RouteEntry> result;
    result.reserve(entries_.size());
    for (const auto &pair : entries_)
        result.push_back(pair.second);
    return result;
}

void RouteTable::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

} // namespace gateway::datasource
