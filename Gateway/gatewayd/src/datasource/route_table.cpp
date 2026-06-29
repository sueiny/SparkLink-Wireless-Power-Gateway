#include "datasource/route_table.h"
#include "common/logger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>

namespace gateway::datasource {
namespace {

std::string trim(const std::string &text)
{
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool parseTrailingNodeId(const std::string &line,
                         uint16_t *node_id,
                         size_t *number_start,
                         std::string *error)
{
    size_t end = line.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(line[end - 1])))
        --end;

    size_t begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(line[begin - 1])))
        --begin;

    if (begin == end) {
        if (error)
            *error = "missing node id in line: " + line;
        return false;
    }

    const int value = std::stoi(line.substr(begin, end - begin));
    if (value <= 0 || value > 65535) {
        if (error)
            *error = "node id out of range in line: " + line;
        return false;
    }

    if (node_id)
        *node_id = static_cast<uint16_t>(value);
    if (number_start)
        *number_start = begin;
    return true;
}

int parseTreeDepth(const std::string &line, size_t number_start)
{
    const std::string prefix = line.substr(0, number_start);
    const size_t pipe_branch = prefix.find("|-");
    const size_t back_branch = prefix.find("`-");

    size_t branch_pos = std::string::npos;
    if (pipe_branch != std::string::npos)
        branch_pos = pipe_branch;
    if (back_branch != std::string::npos)
        branch_pos = branch_pos == std::string::npos
                         ? back_branch
                         : std::min(branch_pos, back_branch);

    if (branch_pos == std::string::npos)
        return 0;

    return static_cast<int>(branch_pos / 3) + 1;
}

uint8_t roleForNode(const RouteEntry &entry)
{
    if (entry.parent_id == 0)
        return codec::SLE_ROLE_ROOT;
    return entry.child_ids.empty() ? codec::SLE_ROLE_LEAF : codec::SLE_ROLE_RELAY;
}

bool parseTopologyText(const uint8_t *payload,
                       uint16_t payload_len,
                       int64_t now_ms,
                       std::map<uint16_t, RouteEntry> *out,
                       std::string *error)
{
    if (payload == nullptr || out == nullptr) {
        if (error)
            *error = "empty topology payload";
        return false;
    }

    const std::string text(reinterpret_cast<const char *>(payload), payload_len);
    std::istringstream iss(text);
    std::string line;
    std::vector<uint16_t> stack;
    int non_empty_line = 0;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (trim(line).empty())
            continue;

        uint16_t node_id = 0;
        size_t number_start = 0;
        if (!parseTrailingNodeId(line, &node_id, &number_start, error))
            return false;

        const int depth = parseTreeDepth(line, number_start);
        if (non_empty_line == 0 && depth != 0) {
            if (error)
                *error = "first topology line must be root: " + line;
            return false;
        }
        if (non_empty_line > 0 && depth == 0) {
            if (error)
                *error = "multiple root lines are not allowed: " + line;
            return false;
        }
        if (depth > static_cast<int>(stack.size())) {
            if (error)
                *error = "topology depth jumps in line: " + line;
            return false;
        }
        if (out->count(node_id) > 0) {
            if (error)
                *error = "duplicate node id: " + std::to_string(node_id);
            return false;
        }

        uint16_t parent_id = 0;
        if (depth > 0) {
            if (static_cast<size_t>(depth) > stack.size()) {
                if (error)
                    *error = "missing parent in line: " + line;
                return false;
            }
            parent_id = stack[depth - 1];
        }

        RouteEntry entry;
        entry.node_id = node_id;
        entry.parent_id = parent_id;
        entry.online = true;
        entry.last_update_ms = now_ms;
        (*out)[node_id] = entry;

        if (parent_id > 0)
            (*out)[parent_id].child_ids.push_back(node_id);

        if (stack.size() <= static_cast<size_t>(depth))
            stack.resize(depth + 1);
        stack[depth] = node_id;
        stack.resize(depth + 1);
        ++non_empty_line;
    }

    if (out->empty()) {
        if (error)
            *error = "topology payload contains no node";
        return false;
    }

    for (auto &item : *out)
        item.second.role = roleForNode(item.second);
    return true;
}

} // namespace

std::map<uint16_t, RouteEntry> RouteTable::aggregateSnapshotsLocked() const
{
    std::map<uint16_t, RouteEntry> result;
    for (const auto &root_item : root_snapshots_) {
        for (const auto &node_item : root_item.second)
            result[node_item.first] = node_item.second;
    }
    return result;
}

RouteUpdateResult RouteTable::updateFromTopologyText(uint16_t report_root_id,
                                                     const uint8_t *payload,
                                                     uint16_t payload_len,
                                                     size_t expected_count,
                                                     const std::vector<model::DtuDeviceInfo> &inventory,
                                                     int64_t now_ms)
{
    RouteUpdateResult result;
    result.expected_count = expected_count;

    std::map<uint16_t, RouteEntry> parsed;
    if (!parseTopologyText(payload, payload_len, now_ms, &parsed, &result.error))
        return result;

    uint16_t root_id = 0;
    for (const auto &item : parsed) {
        if (item.second.parent_id == 0) {
            root_id = item.first;
            break;
        }
    }
    if (root_id == 0) {
        result.error = "topology payload does not contain a root node";
        return result;
    }
    if (root_id != report_root_id) {
        result.error = "topology root does not match ST src_node_id: payload_root=" +
                       std::to_string(root_id) +
                       ", src_node_id=" + std::to_string(report_root_id);
        return result;
    }

    std::set<uint16_t> inventory_nodes;
    for (const auto &dtu : inventory) {
        if (dtu.node_id > 0)
            inventory_nodes.insert(static_cast<uint16_t>(dtu.node_id));
    }
    for (const auto &item : parsed) {
        if (inventory_nodes.count(item.first) == 0) {
            result.error = "topology references unknown DTU node_id=" +
                           std::to_string(item.first);
            return result;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto previous_root_snapshot = root_snapshots_.find(root_id);
    std::map<uint16_t, RouteEntry> previous_snapshot;
    if (previous_root_snapshot != root_snapshots_.end())
        previous_snapshot = previous_root_snapshot->second;

    root_snapshots_[root_id] = parsed;
    std::set<uint16_t> aggregate_nodes;
    for (const auto &root_item : root_snapshots_) {
        for (const auto &node_item : root_item.second) {
            if (!aggregate_nodes.insert(node_item.first).second) {
                const uint16_t duplicate_node_id = node_item.first;
                if (previous_root_snapshot != root_snapshots_.end())
                    root_snapshots_[root_id] = std::move(previous_snapshot);
                else
                    root_snapshots_.erase(root_id);
                result.error = "duplicate DTU node across root snapshots: node_id=" +
                               std::to_string(duplicate_node_id);
                return result;
            }
        }
    }
    const auto aggregate = aggregateSnapshotsLocked();

    size_t observed_inventory_count = 0;
    for (const auto &item : aggregate) {
        if (inventory_nodes.count(item.first) > 0)
            ++observed_inventory_count;
    }

    result.observed_count = observed_inventory_count;
    if (expected_count > 0 && observed_inventory_count > expected_count) {
        if (previous_root_snapshot != root_snapshots_.end())
            root_snapshots_[root_id] = std::move(previous_snapshot);
        else
            root_snapshots_.erase(root_id);
        result.error = "topology node count exceeds expected: observed=" +
                       std::to_string(observed_inventory_count) +
                       ", expected=" + std::to_string(expected_count);
        return result;
    }

    result.ok = true;
    result.complete = expected_count > 0 && observed_inventory_count == expected_count;

    entries_.clear();
    for (const auto &dtu : inventory) {
        if (dtu.node_id <= 0)
            continue;

        RouteEntry entry;
        entry.node_id = static_cast<uint16_t>(dtu.node_id);
        entry.parent_id = 0;
        entry.role = 0;
        entry.online = false;
        entry.last_update_ms = 0;
        entries_[entry.node_id] = entry;
    }

    for (const auto &item : aggregate) {
        auto it = entries_.find(item.first);
        if (it != entries_.end()) {
            it->second = item.second;
            it->second.online = true;
        }
    }

    return result;
}

int RouteTable::findRootByNode(uint16_t node_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(node_id);
    if (it == entries_.end())
        return -1;
    if (!it->second.online)
        return -1;
    // 沿 parent_id 向上查找 Root
    uint16_t current = node_id;
    for (size_t depth = 0; depth < entries_.size() + 1; ++depth) {
        auto jt = entries_.find(current);
        if (jt == entries_.end())
            return -1;
        if (!jt->second.online)
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
    auto it = entries_.find(node_id);
    return it != entries_.end() && it->second.online;
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
    root_snapshots_.clear();
}

} // namespace gateway::datasource
