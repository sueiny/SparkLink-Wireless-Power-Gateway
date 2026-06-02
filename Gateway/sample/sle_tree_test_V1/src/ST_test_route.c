/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test route helpers.
 */
#include "ST_test_internal.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "securec.h"
#include "uart.h"

static void sle_tree_log_route_state(const char *event, uint16_t node_id, uint8_t node_role, uint16_t conn_id)
{
    sle_tree_uart_printf("%s route %s node=%u role=%s via=%u\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, event, node_id, sle_tree_role_name(node_role), conn_id);
}

static bool sle_tree_age_expired(uint32_t now_ms, uint32_t last_seen_ms, uint32_t timeout_ms, uint32_t *age_ms)
{
    int32_t signed_age = (int32_t)(now_ms - last_seen_ms);

    if (signed_age < 0) {
        return false;
    }
    if (age_ms != NULL) {
        *age_ms = (uint32_t)signed_age;
    }
    return (uint32_t)signed_age > timeout_ms;
}

uint16_t sle_tree_node_id_from_addr(const sle_addr_t *addr)
{
    uint16_t node_id;

    if (addr == NULL) {
        return SLE_TREE_INVALID_NODE_ID;
    }
    node_id = ((uint16_t)addr->addr[SLE_ADDR_LEN - 2] << 8) | addr->addr[SLE_ADDR_LEN - 1];
    if (node_id == SLE_TREE_INVALID_NODE_ID) {
        return SLE_TREE_INVALID_NODE_ID;
    }
    return node_id;
}

static void sle_tree_topo_uart_printf(const char *fmt, ...)
{
    va_list args;
    char buffer[256] = {0};
    int ret;

    va_start(args, fmt);
    ret = vsnprintf_s(buffer, sizeof(buffer), sizeof(buffer) - 1, fmt, args);
    va_end(args);
    if (ret <= 0) {
        return;
    }
    (void)uapi_uart_write(SLE_TREE_UART_BUS_ID, (const uint8_t *)buffer, (uint32_t)strlen(buffer), 0);
}

static sle_tree_topo_entry_t *sle_tree_find_topo_entry(uint16_t node_id)
{
    uint8_t i;

    for (i = 0; i < SLE_TREE_MAX_TOPO_NODES; i++) {
        if (g_sle_tree_ctx.topo_entries[i].in_use && g_sle_tree_ctx.topo_entries[i].node_id == node_id) {
            return &g_sle_tree_ctx.topo_entries[i];
        }
    }
    return NULL;
}

static sle_tree_topo_entry_t *sle_tree_alloc_topo_entry(uint16_t node_id)
{
    uint8_t i;
    sle_tree_topo_entry_t *entry = sle_tree_find_topo_entry(node_id);

    if (entry != NULL) {
        return entry;
    }
    for (i = 0; i < SLE_TREE_MAX_TOPO_NODES; i++) {
        if (!g_sle_tree_ctx.topo_entries[i].in_use) {
            entry = &g_sle_tree_ctx.topo_entries[i];
            (void)memset_s(entry, sizeof(*entry), 0, sizeof(*entry));
            entry->in_use = true;
            entry->node_id = node_id;
            return entry;
        }
    }
    return NULL;
}

static void sle_tree_clear_topo_entry(sle_tree_topo_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }
    (void)memset_s(entry, sizeof(*entry), 0, sizeof(*entry));
}

static void sle_tree_remove_topo_subtree(uint16_t node_id)
{
    uint8_t i;
    sle_tree_topo_entry_t *entry;

    for (i = 0; i < SLE_TREE_MAX_TOPO_NODES; i++) {
        if (g_sle_tree_ctx.topo_entries[i].in_use && g_sle_tree_ctx.topo_entries[i].parent_node_id == node_id) {
            sle_tree_remove_topo_subtree(g_sle_tree_ctx.topo_entries[i].node_id);
        }
    }
    entry = sle_tree_find_topo_entry(node_id);
    if (entry != NULL) {
        sle_tree_uart_printf("%s topo drop node=%u parent=%u role=%s\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, entry->node_id, entry->parent_node_id, sle_tree_role_name(entry->node_role));
    }
    sle_tree_clear_topo_entry(entry);
}

void sle_tree_root_touch_direct_child(uint16_t node_id, uint8_t node_role)
{
    sle_tree_topo_entry_t *entry;
    bool is_new;

    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_ROOT || node_id == SLE_TREE_INVALID_NODE_ID || node_role == 0) {
        return;
    }
    is_new = (sle_tree_find_topo_entry(node_id) == NULL);
    entry = sle_tree_alloc_topo_entry(node_id);
    if (entry == NULL) {
        return;
    }
    if (is_new || entry->parent_node_id != g_sle_tree_ctx.cfg.node_id) {
        entry->parent_node_id = g_sle_tree_ctx.cfg.node_id;
        entry->node_role = node_role;
        entry->last_seen_ms = (uint32_t)sle_tree_now_ms();
        sle_tree_root_print_topology_tree();
    } else {
        entry->last_seen_ms = (uint32_t)sle_tree_now_ms();
    }
}

void sle_tree_root_remove_direct_child(uint16_t node_id)
{
    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_ROOT || node_id == SLE_TREE_INVALID_NODE_ID) {
        return;
    }
    sle_tree_remove_topo_subtree(node_id);
    sle_tree_root_print_topology_tree();
}

bool sle_tree_root_process_topo_summary(const uint8_t *payload, uint16_t payload_len)
{
    uint16_t owner_node_id;
    uint8_t child_count;
    uint16_t cursor = 3;
    uint8_t i;
    uint32_t now_ms = (uint32_t)sle_tree_now_ms();
    sle_tree_topo_entry_t *owner_entry;
    bool changed = false;

    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_ROOT || payload == NULL || payload_len < 3) {
        return false;
    }
    owner_node_id = sle_tree_get_le16(&payload[0]);
    child_count = payload[2];
    if (payload_len < (uint16_t)(3U + ((uint16_t)child_count * 3U))) {
        return false;
    }
    sle_tree_uart_printf("%s topo summary recv from=%u children=%u\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, owner_node_id, child_count);

    owner_entry = sle_tree_alloc_topo_entry(owner_node_id);
    if (owner_entry != NULL) {
        if (owner_entry->node_role == 0) {
            owner_entry->node_role = SLE_TREE_ROLE_RELAY;
            changed = true;
        }
        owner_entry->last_seen_ms = now_ms;
    }

    /* 先删除该 relay 的所有旧子节点条目，再根据新 summary 重建。
     * 防止已断连的子节点条目残留在 root 拓扑中直到超时。 */
    for (i = 0; i < SLE_TREE_MAX_TOPO_NODES; i++) {
        sle_tree_topo_entry_t *old = &g_sle_tree_ctx.topo_entries[i];
        if (old->in_use && old->parent_node_id == owner_node_id) {
            sle_tree_uart_printf("%s topo prune stale child=%u parent=%u\r\n",
                SLE_TREE_SERVER_LOG_PREFIX, old->node_id, old->parent_node_id);
            old->in_use = false;
            changed = true;
        }
    }

    for (i = 0; i < child_count; i++) {
        uint16_t child_node_id = sle_tree_get_le16(&payload[cursor]);
        uint8_t child_role = payload[cursor + 2];
        bool is_new = (sle_tree_find_topo_entry(child_node_id) == NULL);
        sle_tree_topo_entry_t *entry = sle_tree_alloc_topo_entry(child_node_id);

        if (entry != NULL) {
            if (is_new || entry->parent_node_id != owner_node_id || entry->node_role != child_role) {
                changed = true;
            }
            entry->parent_node_id = owner_node_id;
            entry->node_role = child_role;
            entry->last_seen_ms = now_ms;
        }
        cursor = (uint16_t)(cursor + 3U);
    }
    return changed;
}

void sle_tree_root_refresh_topo_activity(uint16_t node_id)
{
    sle_tree_topo_entry_t *entry;

    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_ROOT || node_id == SLE_TREE_INVALID_NODE_ID) {
        return;
    }
    entry = sle_tree_find_topo_entry(node_id);
    if (entry != NULL) {
        entry->last_seen_ms = (uint32_t)sle_tree_now_ms();
    }
}

void sle_tree_root_remove_stale_topology(void)
{
    uint8_t i;
    uint32_t now_ms = (uint32_t)sle_tree_now_ms();

    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_ROOT) {
        return;
    }
    for (i = 0; i < SLE_TREE_MAX_TOPO_NODES; i++) {
        sle_tree_topo_entry_t *entry = &g_sle_tree_ctx.topo_entries[i];
        uint32_t age_ms;

        if (!entry->in_use) {
            continue;
        }
        if (sle_tree_age_expired(now_ms, entry->last_seen_ms, SLE_TREE_TOPO_TIMEOUT_MS, &age_ms)) {
            sle_tree_uart_printf("%s topo stale node=%u parent=%u age=%u\r\n",
                SLE_TREE_SERVER_LOG_PREFIX, entry->node_id, entry->parent_node_id, age_ms);
            sle_tree_remove_topo_subtree(entry->node_id);
        }
    }
}

static uint16_t sle_tree_find_next_topo_child(uint16_t parent_node_id, uint16_t prev_node_id)
{
    uint8_t i;
    uint16_t next_node_id = SLE_TREE_INVALID_NODE_ID;

    for (i = 0; i < SLE_TREE_MAX_TOPO_NODES; i++) {
        sle_tree_topo_entry_t *entry = &g_sle_tree_ctx.topo_entries[i];

        if (!entry->in_use || entry->parent_node_id != parent_node_id || entry->node_id <= prev_node_id) {
            continue;
        }
        if (next_node_id == SLE_TREE_INVALID_NODE_ID || entry->node_id < next_node_id) {
            next_node_id = entry->node_id;
        }
    }
    return next_node_id;
}

static void sle_tree_print_topology_subtree(uint16_t node_id, const char *prefix, bool is_last)
{
    char next_prefix[96] = {0};
    uint16_t child_node_id;
    uint16_t next_after_child;
    const char *branch = is_last ? "`- " : "|- ";
    const char *pad = is_last ? "   " : "|  ";

    sle_tree_topo_uart_printf("%s%s%u\r\n", prefix, branch, node_id);
    (void)snprintf_s(next_prefix, sizeof(next_prefix), sizeof(next_prefix) - 1, "%s%s", prefix, pad);

    child_node_id = sle_tree_find_next_topo_child(node_id, 0);
    while (child_node_id != SLE_TREE_INVALID_NODE_ID) {
        next_after_child = sle_tree_find_next_topo_child(node_id, child_node_id);
        sle_tree_print_topology_subtree(child_node_id, next_prefix, next_after_child == SLE_TREE_INVALID_NODE_ID);
        child_node_id = next_after_child;
    }
}

void sle_tree_root_print_topology_tree(void)
{
    uint16_t child_node_id;
    uint16_t next_after_child;

    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_ROOT) {
        return;
    }
    sle_tree_topo_uart_printf("%s topo tree begin\r\n", SLE_TREE_SERVER_LOG_PREFIX);
    sle_tree_topo_uart_printf("%u\r\n", g_sle_tree_ctx.cfg.node_id);
    child_node_id = sle_tree_find_next_topo_child(g_sle_tree_ctx.cfg.node_id, 0);
    while (child_node_id != SLE_TREE_INVALID_NODE_ID) {
        next_after_child = sle_tree_find_next_topo_child(g_sle_tree_ctx.cfg.node_id, child_node_id);
        sle_tree_print_topology_subtree(child_node_id, "", next_after_child == SLE_TREE_INVALID_NODE_ID);
        child_node_id = next_after_child;
    }
    sle_tree_topo_uart_printf("%s topo tree end\r\n", SLE_TREE_SERVER_LOG_PREFIX);
}

/*--------------------------------------------------------------------------
 * Child link helpers
 * 子连接管理辅助
 *--------------------------------------------------------------------------*/

/**
 * @brief Count current direct children connected to root / relay.
 * @brief 统计当前 root / relay 的直连子节点数量。
 */
uint8_t sle_tree_count_children(void)
{
    uint8_t i;
    uint8_t count = 0;

    for (i = 0; i < SLE_TREE_MAX_CHILDREN; i++) {
        if (g_sle_tree_ctx.children[i].in_use) {
            count++;
        }
    }
    return count;
}

uint8_t sle_tree_max_children_for_role(void)
{
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        return SLE_TREE_MAX_CHILDREN;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        return (uint8_t)(SLE_TREE_MAX_CHILDREN - 1U);
    }
    return 0;
}

/*--------------------------------------------------------------------------
 * Route table helpers
 * 路由表辅助
 *--------------------------------------------------------------------------*/

/**
 * @brief Find an existing route entry by destination node_id.
 * @brief 根据目标 node_id 查找已有路由项。
 */
sle_tree_route_t *sle_tree_find_route(uint16_t node_id)
{
    uint8_t i;

    for (i = 0; i < SLE_TREE_MAX_ROUTES; i++) {
        if (g_sle_tree_ctx.routes[i].in_use && g_sle_tree_ctx.routes[i].node_id == node_id) {
            return &g_sle_tree_ctx.routes[i];
        }
    }
    return NULL;
}

/**
 * @brief Learn or refresh one route item from received child traffic.
 * @brief 根据收到的子节点流量学习或刷新一条路由。
 */
void sle_tree_learn_route(uint16_t node_id, uint8_t node_role, uint16_t conn_id)
{
    uint8_t i;
    sle_tree_route_t *route;

    if (node_id == SLE_TREE_INVALID_NODE_ID || conn_id == SLE_TREE_INVALID_CONN_ID) {
        return;
    }
    route = sle_tree_find_route(node_id);
    if (route == NULL) {
        for (i = 0; i < SLE_TREE_MAX_ROUTES; i++) {
            if (!g_sle_tree_ctx.routes[i].in_use) {
                route = &g_sle_tree_ctx.routes[i];
                (void)memset_s(route, sizeof(*route), 0, sizeof(*route));
                route->in_use = true;
                route->node_id = node_id;
                break;
            }
        }
    }
    if (route == NULL) {
        return;
    }
    if (!route->in_use || route->next_hop_conn_id != conn_id || route->node_role != node_role) {
        sle_tree_log_route_state("learn", node_id, node_role, conn_id);
    }
    route->node_role = node_role;
    route->next_hop_conn_id = conn_id;
    route->last_seen_ms = (uint32_t)sle_tree_now_ms();
}

/**
 * @brief Remove all learned routes whose next hop is the disconnected child.
 * @brief 删除所有下一跳是该断开子节点的路由项。
 */
static void sle_tree_remove_routes_by_conn(uint16_t conn_id)
{
    uint8_t i;

    for (i = 0; i < SLE_TREE_MAX_ROUTES; i++) {
        if (g_sle_tree_ctx.routes[i].in_use && g_sle_tree_ctx.routes[i].next_hop_conn_id == conn_id) {
            sle_tree_log_route_state("drop", g_sle_tree_ctx.routes[i].node_id,
                g_sle_tree_ctx.routes[i].node_role, conn_id);
            (void)memset_s(&g_sle_tree_ctx.routes[i], sizeof(g_sle_tree_ctx.routes[i]), 0,
                sizeof(g_sle_tree_ctx.routes[i]));
        }
    }
}

/**
 * @brief Drop stale routes that no longer receive heartbeat/data refresh.
 * @brief 清理超时未被心跳/数据刷新过的旧路由。
 */
void sle_tree_remove_stale_routes(void)
{
    uint8_t i;
    uint32_t now_ms = (uint32_t)sle_tree_now_ms();

    for (i = 0; i < SLE_TREE_MAX_ROUTES; i++) {
        uint32_t age_ms;

        if (!g_sle_tree_ctx.routes[i].in_use) {
            continue;
        }
        if (sle_tree_age_expired(now_ms, g_sle_tree_ctx.routes[i].last_seen_ms, SLE_TREE_ROUTE_TIMEOUT_MS, &age_ms)) {
            sle_tree_log_route_state("stale", g_sle_tree_ctx.routes[i].node_id,
                g_sle_tree_ctx.routes[i].node_role, g_sle_tree_ctx.routes[i].next_hop_conn_id);
            (void)memset_s(&g_sle_tree_ctx.routes[i], sizeof(g_sle_tree_ctx.routes[i]), 0,
                sizeof(g_sle_tree_ctx.routes[i]));
        }
    }
}

/**
 * @brief Find a direct child link slot by SLE connection id.
 * @brief 根据 SLE 连接 ID 查找直连子节点槽位。
 */
sle_tree_child_link_t *sle_tree_find_child_by_conn(uint16_t conn_id)
{
    uint8_t i;

    for (i = 0; i < SLE_TREE_MAX_CHILDREN; i++) {
        if (g_sle_tree_ctx.children[i].in_use && g_sle_tree_ctx.children[i].conn_id == conn_id) {
            return &g_sle_tree_ctx.children[i];
        }
    }
    return NULL;
}

void sle_tree_root_touch_route_activity(uint16_t node_id, uint8_t node_role, uint16_t conn_id)
{
    sle_tree_child_link_t *child;
    sle_tree_topo_entry_t *entry;
    uint32_t now_ms = (uint32_t)sle_tree_now_ms();

    if (g_sle_tree_ctx.role != SLE_TREE_ROLE_ROOT) {
        return;
    }

    child = sle_tree_find_child_by_conn(conn_id);
    if (child != NULL && child->direct_node_id != SLE_TREE_INVALID_NODE_ID && child->direct_role != 0) {
        entry = sle_tree_alloc_topo_entry(child->direct_node_id);
        if (entry != NULL) {
            entry->parent_node_id = g_sle_tree_ctx.cfg.node_id;
            entry->node_role = child->direct_role;
            entry->last_seen_ms = now_ms;
        }
    }

    entry = sle_tree_find_topo_entry(node_id);
    if (entry != NULL) {
        if (node_role != 0) {
            entry->node_role = node_role;
        }
        entry->last_seen_ms = now_ms;
    }
}

/**
 * @brief Allocate or reuse one direct child slot for a new server-side link.
 * @brief 为新的 server 侧链路分配或复用一个直连子节点槽位。
 */
sle_tree_child_link_t *sle_tree_alloc_child(uint16_t conn_id, const sle_addr_t *addr)
{
    uint8_t i;
    sle_tree_child_link_t *child;

    child = sle_tree_find_child_by_conn(conn_id);
    if (child != NULL) {
        return child;
    }
    if (sle_tree_count_children() >= sle_tree_max_children_for_role()) {
        return NULL;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY &&
        g_sle_tree_ctx.uplink.connected &&
        g_sle_tree_ctx.uplink.depth + 1 >= SLE_TREE_MAX_DEPTH) {
        return NULL;
    }
    for (i = 0; i < SLE_TREE_MAX_CHILDREN; i++) {
        if (!g_sle_tree_ctx.children[i].in_use) {
            child = &g_sle_tree_ctx.children[i];
            (void)memset_s(child, sizeof(*child), 0, sizeof(*child));
            child->in_use = true;
            child->conn_id = conn_id;
            child->direct_node_id = sle_tree_node_id_from_addr(addr);
            child->direct_role = 0;
            if (addr != NULL) {
                (void)memcpy_s(&child->addr, sizeof(child->addr), addr, sizeof(*addr));
            }
            sle_tree_uart_printf("%s child add conn=%u\r\n", SLE_TREE_SERVER_LOG_PREFIX, conn_id);
            if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT &&
                child->direct_node_id != SLE_TREE_INVALID_NODE_ID) {
                /* In this demo, root direct children are relay-role devices.
                 * 该 demo 下 root 的直连子节点均为 relay，先入拓扑后续再由业务帧刷新。 */
                child->direct_role = SLE_TREE_ROLE_RELAY;
                sle_tree_root_touch_direct_child(child->direct_node_id, child->direct_role);
            }
            return child;
        }
    }
    return NULL;
}

/**
 * @brief Remove one direct child and all routes behind that child.
 * @brief 删除一个直连子节点，并清理其后面的全部路由。
 */
void sle_tree_remove_child(uint16_t conn_id)
{
    sle_tree_child_link_t *child = sle_tree_find_child_by_conn(conn_id);

    if (child == NULL) {
        return;
    }
    sle_tree_uart_printf("%s child del conn=%u node=%u role=%s\r\n", SLE_TREE_SERVER_LOG_PREFIX,
        conn_id, child->direct_node_id, sle_tree_role_name(child->direct_role));
    sle_tree_root_remove_direct_child(child->direct_node_id);
    (void)memset_s(child, sizeof(*child), 0, sizeof(*child));
    sle_tree_remove_routes_by_conn(conn_id);
    sle_tree_send_topo_summary();
}

/**
 * @brief Disconnect all direct children and clean up routes, used when relay depth changes.
 * @brief relay 深度变化时断开所有直连子节点，强制它们重新选父以修正 depth。
 */
void sle_tree_relay_drop_all_children(const char *reason)
{
    uint8_t i;

    for (i = 0; i < SLE_TREE_MAX_CHILDREN; i++) {
        sle_tree_child_link_t *child = &g_sle_tree_ctx.children[i];
        if (!child->in_use) {
            continue;
        }
        sle_tree_uart_printf("%s force drop child conn=%u node=%u reason=%s\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, child->conn_id, child->direct_node_id, reason);
        sle_tree_root_remove_direct_child(child->direct_node_id);
        sle_tree_remove_routes_by_conn(child->conn_id);
        (void)sle_disconnect_remote_device(&child->addr);
        (void)memset_s(child, sizeof(*child), 0, sizeof(*child));
    }
    sle_tree_send_topo_summary();
}

/**
 * @brief Mark the identity of the direct child after first valid frame arrives.
 * @brief 在收到首个有效帧后，标记该直连子节点的身份信息。
 */
bool sle_tree_mark_child_identity(uint16_t conn_id, uint16_t node_id, uint8_t node_role)
{
    sle_tree_child_link_t *child = sle_tree_find_child_by_conn(conn_id);
    bool update_identity = true;
    bool changed = false;

    if (child == NULL) {
        return false;
    }
    if (child->direct_node_id != SLE_TREE_INVALID_NODE_ID && child->direct_node_id != node_id) {
        return false;
    }
    if (child->direct_role == SLE_TREE_ROLE_RELAY && node_role != SLE_TREE_ROLE_RELAY) {
        update_identity = false;
    }
    if (node_role == SLE_TREE_ROLE_RELAY) {
        update_identity = true;
    }
    if (!update_identity) {
        return false;
    }
    if (child->direct_node_id != node_id || child->direct_role != node_role) {
        changed = true;
        sle_tree_uart_printf("%s child identify conn=%u node=%u role=%s\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, node_id, sle_tree_role_name(node_role));
    }
    child->direct_node_id = node_id;
    child->direct_role = node_role;
    sle_tree_root_touch_direct_child(node_id, node_role);
    return changed;
}

/**
 * @brief Pick the first known non-root route for root-side auto downlink test.
 * @brief 为 root 自动下行测试选择第一条已知非 root 节点路由。
 */
sle_tree_route_t *sle_tree_pick_first_node_route(void)
{
    uint8_t i;

    for (i = 0; i < SLE_TREE_MAX_ROUTES; i++) {
        if (g_sle_tree_ctx.routes[i].in_use && g_sle_tree_ctx.routes[i].node_id != g_sle_tree_ctx.cfg.node_id) {
            return &g_sle_tree_ctx.routes[i];
        }
    }
    return NULL;
}
