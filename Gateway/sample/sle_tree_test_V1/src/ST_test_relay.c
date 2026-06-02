/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test relay role entry.
 */
#include "app_init.h"
#include <string.h>

#include "common_def.h"
#include "securec.h"
#include "sle_ssap_client.h"
#include "ST_test_internal.h"

/*--------------------------------------------------------------------------
 * Relay role forwarding logic
 *--------------------------------------------------------------------------*/

/**
 * @brief Forward child-originated traffic upward or sideways through learned routes.
 */
void sle_tree_relay_handle_frame_from_child(uint16_t conn_id, const sle_tree_frame_view_t *frame,
    const uint8_t *data, uint16_t data_len)
{
    sle_tree_route_t *route;
    uint8_t forward_buf[SLE_TREE_MAX_FRAME_LEN] = {0};

    if (frame == NULL) {
        return;
    }
    if (frame->frame_type == SLE_TREE_FRAME_TYPE_HEARTBEAT) {
        sle_tree_handle_frame_to_self(frame, conn_id);
        if (g_sle_tree_ctx.uplink.connected && g_sle_tree_ctx.uplink.handle_ready) {
            (void)memcpy_s(forward_buf, sizeof(forward_buf), data, data_len);
            (void)sle_tree_send_uplink_write(forward_buf, data_len);
        } else {
            sle_tree_frame_queue_enqueue(data, data_len);
        }
        return;
    }
    if (frame->dst_node_id == g_sle_tree_ctx.cfg.node_id) {
        sle_tree_handle_frame_to_self(frame, conn_id);
        return;
    }

    route = sle_tree_find_route(frame->dst_node_id);
    if (route != NULL && route->next_hop_conn_id != conn_id) {
        (void)memcpy_s(forward_buf, sizeof(forward_buf), data, data_len);
        if (sle_tree_send_notify(route->next_hop_conn_id, forward_buf, data_len) == ERRCODE_SUCC) {
            return;
        }
    }
    if (g_sle_tree_ctx.uplink.connected && g_sle_tree_ctx.uplink.handle_ready) {
        (void)memcpy_s(forward_buf, sizeof(forward_buf), data, data_len);
        if (sle_tree_send_uplink_write(forward_buf, data_len) == ERRCODE_SUCC) {
            return;
        }
    }
    sle_tree_frame_queue_enqueue(data, data_len);
}

/**
 * @brief Forward parent-originated traffic downward to the target child.
 */
void sle_tree_relay_handle_frame_from_parent(const sle_tree_frame_view_t *frame, const uint8_t *data,
    uint16_t data_len)
{
    sle_tree_route_t *route;
    uint8_t forward_buf[SLE_TREE_MAX_FRAME_LEN] = {0};

    if (frame == NULL) {
        return;
    }
    /* 深度更新通知：父节点重连后深度变化，通知本 relay 更新并转发给子节点 */
    if (frame->frame_type == SLE_TREE_FRAME_TYPE_DEPTH_UPDATE &&
        frame->payload_len >= 1 && frame->payload != NULL) {
        uint8_t new_parent_depth = frame->payload[0];
        uint8_t new_depth = (uint8_t)(new_parent_depth + 1U);
        sle_tree_uart_printf("%s depth update from parent: parent_depth=%u new_depth=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, new_parent_depth, new_depth);
        g_sle_tree_ctx.uplink.depth = new_depth;
        if (new_depth >= SLE_TREE_MAX_DEPTH) {
            /* 本 relay 深度已到上限，断开父节点，变成叶子节点 */
            sle_tree_uart_printf("%s depth exceeded MAX, disconnect from parent\r\n",
                SLE_TREE_SERVER_LOG_PREFIX);
            sle_tree_handle_uplink_disconnected();
            return;
        }
        /* 转发给所有子节点，让它们也更新深度 */
        sle_tree_send_depth_update_to_children(new_depth);
        return;
    }
    if (frame->dst_node_id == g_sle_tree_ctx.cfg.node_id || frame->dst_node_id == SLE_TREE_ANY_NODE_ID) {
        sle_tree_handle_frame_to_self(frame, g_sle_tree_ctx.uplink.conn_id);
        return;
    }
    route = sle_tree_find_route(frame->dst_node_id);
    if (route != NULL) {
        (void)memcpy_s(forward_buf, sizeof(forward_buf), data, data_len);
        if (sle_tree_send_notify(route->next_hop_conn_id, forward_buf, data_len) == ERRCODE_SUCC) {
            return;
        }
    }
    sle_tree_report_unreachable(frame->dst_node_id);
}

/*--------------------------------------------------------------------------
 * Relay uplink lifecycle
 *--------------------------------------------------------------------------*/

/**
 * @brief Check whether one address belongs to relay uplink or pending parent candidate.
 */
bool sle_tree_relay_is_uplink_addr(const sle_addr_t *addr)
{
    uint16_t node_id;

    if (addr == NULL) {
        return false;
    }
    node_id = sle_tree_node_id_from_addr(addr);
    if (g_sle_tree_ctx.pending_parent.valid && sle_tree_addr_equal(addr, &g_sle_tree_ctx.pending_parent.addr)) {
        return true;
    }
    if (g_sle_tree_ctx.pending_parent.valid && node_id == g_sle_tree_ctx.pending_parent.node_id) {
        return true;
    }
    if (!g_sle_tree_ctx.uplink.connected && g_sle_tree_ctx.cfg.last_parent_node_id != SLE_TREE_INVALID_LAST_PARENT &&
        node_id == g_sle_tree_ctx.cfg.last_parent_node_id) {
        return true;
    }
    if (g_sle_tree_ctx.uplink.connected && sle_tree_addr_equal(addr, &g_sle_tree_ctx.uplink.addr)) {
        return true;
    }
    if (g_sle_tree_ctx.uplink.connected && node_id == g_sle_tree_ctx.uplink.parent_node_id) {
        return true;
    }
    return false;
}

/**
 * @brief Finish relay uplink state after connecting to root.
 */
void sle_tree_relay_handle_uplink_connected(uint16_t conn_id, const sle_addr_t *addr, sle_pair_state_t pair_state)
{
    g_sle_tree_ctx.uplink.connected = true;
    g_sle_tree_ctx.uplink.conn_id = conn_id;
    g_sle_tree_ctx.uplink.handle_ready = false;
    g_sle_tree_ctx.uplink.write_handle = 0;
    g_sle_tree_ctx.reparent_pending = false;
    g_sle_tree_ctx.optimize_scan_active = false;
    if (addr != NULL) {
        (void)memcpy_s(&g_sle_tree_ctx.uplink.addr, sizeof(g_sle_tree_ctx.uplink.addr), addr, sizeof(*addr));
    }
    sle_tree_cache_pending_parent_to_uplink();
    if (g_sle_tree_ctx.uplink.parent_node_id == SLE_TREE_INVALID_NODE_ID && addr != NULL) {
        g_sle_tree_ctx.uplink.parent_node_id = sle_tree_node_id_from_addr(addr);
    }
    /* 深度变化检测：如果 relay 重连后深度变了，通知子节点更新深度，
     * 而不是直接断开。子节点收到通知后自行决定是否需要断开。 */
    if (g_sle_tree_ctx.prev_depth != 0 &&
        g_sle_tree_ctx.uplink.depth != g_sle_tree_ctx.prev_depth) {
        sle_tree_uart_printf("%s depth changed %u->%u, notify children\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.prev_depth, g_sle_tree_ctx.uplink.depth);
        sle_tree_send_depth_update_to_children(g_sle_tree_ctx.uplink.depth);
    }
    g_sle_tree_ctx.prev_depth = 0;
    if (pair_state == SLE_PAIR_NONE && addr != NULL) {
        errcode_t ret = sle_pair_remote_device(addr);
        sle_tree_uart_printf("%s pair start conn=%u node=%u ret=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, g_sle_tree_ctx.uplink.parent_node_id, ret);
        if (ret != ERRCODE_SUCC) {
            sle_tree_handle_uplink_disconnected();
        }
    } else {
        ssap_exchange_info_t info = {0};
        errcode_t ret;
        info.mtu_size = SLE_TREE_MTU_SIZE;
        info.version = 1;
        ret = ssapc_exchange_info_req(SLE_TREE_CLIENT_ID, conn_id, &info);
        sle_tree_uart_printf("%s uplink exchange start conn=%u reason=already_paired ret=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, ret);
        if (ret != ERRCODE_SUCC) {
            if (g_sle_tree_ctx.uplink.parent_node_id != SLE_TREE_INVALID_NODE_ID) {
                sle_tree_mark_parent_connect_failed(g_sle_tree_ctx.uplink.parent_node_id);
            }
            if (addr != NULL) {
                (void)sle_remove_paired_remote_device(addr);
            }
            sle_tree_handle_uplink_disconnected();
        }
    }
    sle_tree_refresh_advertising();
}

/**
 * @brief Clear uplink state and restart advertising/rescan after parent disconnect.
 */
void sle_tree_relay_handle_uplink_disconnected(void)
{
    sle_tree_pending_parent_t next_parent = g_sle_tree_ctx.pending_parent;

    g_sle_tree_ctx.prev_depth = g_sle_tree_ctx.uplink.depth;
    (void)memset_s(&g_sle_tree_ctx.uplink, sizeof(g_sle_tree_ctx.uplink), 0, sizeof(g_sle_tree_ctx.uplink));
    g_sle_tree_ctx.parent_connect_start_ms = 0;
    sle_tree_refresh_advertising();
    if (g_sle_tree_ctx.reparent_pending && next_parent.valid) {
        errcode_t ret = sle_connect_remote_device(&next_parent.addr);

        if (ret == ERRCODE_SUCC) {
            g_sle_tree_ctx.pending_parent = next_parent;
            g_sle_tree_ctx.reparent_pending = false;
            g_sle_tree_ctx.optimize_scan_active = false;
            return;
        }
    }
    (void)memset_s(&g_sle_tree_ctx.pending_parent, sizeof(g_sle_tree_ctx.pending_parent), 0,
        sizeof(g_sle_tree_ctx.pending_parent));
    g_sle_tree_ctx.reparent_pending = false;
    g_sle_tree_ctx.optimize_scan_active = false;
    /* 级联防护：根据节点深度增加额外延迟，避免depth-1断开后子节点同时涌入重连 */
    uint32_t cascade_delay = (uint32_t)g_sle_tree_ctx.prev_depth * 500;
    sle_tree_schedule_rescan(SLE_TREE_RESCAN_DELAY_MS + cascade_delay);
}

/*--------------------------------------------------------------------------
 * Relay background hooks
 *--------------------------------------------------------------------------*/

void sle_tree_relay_try_send_auto_traffic(void)
{
#if defined(CONFIG_SLE_TREE_TEST_ENABLE_AUTO_TRAFFIC) && defined(CONFIG_SAMPLE_SUPPORT_SLE_TREE_RELAY_SAMPLE)
    const uint8_t payload[] = "AUTO-UP";

    if (CONFIG_SLE_TREE_TEST_AUTO_UPLINK_DST_NODE_ID == g_sle_tree_ctx.cfg.node_id) {
        sle_tree_uart_printf("%s relay auto-up skipped: dst_node_id=%u equals local node_id\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.cfg.node_id);
        return;
    }
    if (!g_sle_tree_ctx.uplink.connected || !g_sle_tree_ctx.uplink.handle_ready) {
        return;
    }
    sle_tree_send_local_data(CONFIG_SLE_TREE_TEST_AUTO_UPLINK_DST_NODE_ID, payload, sizeof(payload) - 1U);
#endif
}

/**
 * @brief Relay currently has no extra per-loop maintenance beyond common logic.
 */
void sle_tree_relay_role_tick(uint64_t now_ms)
{
    /* [测试模式] 候选选择已移至 sle_tree_store_candidate，此处仅处理扫描超时重调度 */
    if (g_sle_tree_ctx.seeking && (now_ms - g_sle_tree_ctx.scan_start_ms) >= SLE_TREE_SCAN_COLLECT_WINDOW_MS) {
        g_sle_tree_ctx.seeking = false;
        sle_tree_schedule_rescan(SLE_TREE_RESCAN_DELAY_MS);
    }
    if (sle_tree_should_optimize_parent(now_ms)) {
        g_sle_tree_ctx.last_optimize_scan_ms = now_ms;
        g_sle_tree_ctx.optimize_scan_active = true;
        sle_tree_start_scan();
    }
    /* 周期性拓扑摘要：作为事件驱动通知的兜底，防止单次丢失导致 root 拓扑过时 */
    if (g_sle_tree_ctx.uplink.connected && g_sle_tree_ctx.uplink.handle_ready &&
        (now_ms - g_sle_tree_ctx.last_topo_summary_ms) >= SLE_TREE_TOPO_SUMMARY_PERIOD_MS) {
        g_sle_tree_ctx.last_topo_summary_ms = now_ms;
        sle_tree_send_topo_summary();
    }
    /* uplink 恢复后，发送缓存的帧 */
    if (g_sle_tree_ctx.uplink.connected && g_sle_tree_ctx.uplink.handle_ready &&
        g_sle_tree_ctx.frame_queue.count > 0) {
        sle_tree_frame_queue_flush();
    }
}

/**
 * @brief Relay accepts already-joined root/relay advertisements as parent candidates.
 */
void sle_tree_relay_handle_seek_result(sle_seek_result_info_t *result, const sle_tree_adv_info_t *adv_info)
{
    if (result == NULL || adv_info == NULL) {
        return;
    }
    if (adv_info->role == SLE_TREE_ROLE_ROOT) {
        sle_tree_store_candidate(&result->addr, adv_info, (int8_t)result->rssi);
        return;
    }
    if (adv_info->role != SLE_TREE_ROLE_RELAY) {
        return;
    }
    if (adv_info->root_node_id == SLE_TREE_INVALID_NODE_ID || adv_info->depth >= (SLE_TREE_MAX_DEPTH - 1U)) {
        return;
    }
    if (g_sle_tree_ctx.uplink.connected && adv_info->depth >= g_sle_tree_ctx.uplink.depth) {
        return;
    }
    sle_tree_store_candidate(&result->addr, adv_info, (int8_t)result->rssi);
}

#if defined(CONFIG_SAMPLE_SUPPORT_SLE_TREE_RELAY_SAMPLE)
static void sle_tree_test_entry(void)
{
    (void)sle_tree_test_relay_init();
}

app_run(sle_tree_test_entry);
#endif
