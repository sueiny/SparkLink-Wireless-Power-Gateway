/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test leaf role entry.
 */
#include "app_init.h"
#include "common_def.h"
#include "securec.h"
#include "sle_ssap_client.h"
#include "ST_test_internal.h"

/*--------------------------------------------------------------------------
 * Leaf role forwarding logic
 *--------------------------------------------------------------------------*/

/**
 * @brief Leaf only consumes downlink traffic addressed to itself/broadcast heartbeat.
 */
void sle_tree_leaf_handle_frame_from_parent(const sle_tree_frame_view_t *frame)
{
    if (frame == NULL) {
        return;
    }
    /* 深度更新通知：父节点重连后深度变化，更新自己的深度 */
    if (frame->frame_type == SLE_TREE_FRAME_TYPE_DEPTH_UPDATE &&
        frame->payload_len >= 1 && frame->payload != NULL) {
        uint8_t new_parent_depth = frame->payload[0];
        uint8_t new_depth = (uint8_t)(new_parent_depth + 1U);
        sle_tree_uart_printf("%s depth update from parent: parent_depth=%u new_depth=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, new_parent_depth, new_depth);
        g_sle_tree_ctx.uplink.depth = new_depth;
        if (new_depth >= SLE_TREE_MAX_DEPTH) {
            /* 深度已到上限，断开变成游离节点重新选父 */
            sle_tree_uart_printf("%s depth exceeded MAX, disconnect\r\n", SLE_TREE_SERVER_LOG_PREFIX);
            sle_tree_handle_uplink_disconnected();
        }
        return;
    }
    if (frame->dst_node_id == g_sle_tree_ctx.cfg.node_id || frame->dst_node_id == SLE_TREE_ANY_NODE_ID) {
        sle_tree_handle_frame_to_self(frame, g_sle_tree_ctx.uplink.conn_id);
        return;
    }
    sle_tree_report_unreachable(frame->dst_node_id);
}

/*--------------------------------------------------------------------------
 * Leaf uplink lifecycle
 *--------------------------------------------------------------------------*/

/**
 * @brief Leaf has exactly one uplink parent, so all parent addresses are accepted.
 */
bool sle_tree_leaf_is_uplink_addr(const sle_addr_t *addr)
{
    unused(addr);
    return true;
}

/**
 * @brief Finish leaf uplink state after connecting to selected relay.
 */
void sle_tree_leaf_handle_uplink_connected(uint16_t conn_id, const sle_addr_t *addr, sle_pair_state_t pair_state)
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
}

/**
 * @brief Clear leaf uplink state and schedule a new parent selection round.
 */
void sle_tree_leaf_handle_uplink_disconnected(void)
{
    sle_tree_pending_parent_t next_parent = g_sle_tree_ctx.pending_parent;

    (void)memset_s(&g_sle_tree_ctx.uplink, sizeof(g_sle_tree_ctx.uplink), 0, sizeof(g_sle_tree_ctx.uplink));
    g_sle_tree_ctx.parent_connect_start_ms = 0;
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
    /* 级联防护：leaf固定增加500ms延迟，避免同时涌入重连 */
    sle_tree_schedule_rescan(SLE_TREE_RESCAN_DELAY_MS + 500);
}

/*--------------------------------------------------------------------------
 * Leaf background hooks
 *--------------------------------------------------------------------------*/

/**
 * @brief Emit periodic auto uplink test traffic when feature is enabled.
 */
void sle_tree_leaf_try_send_auto_traffic(void)
{
#if defined(CONFIG_SLE_TREE_TEST_ENABLE_AUTO_TRAFFIC) && defined(CONFIG_SAMPLE_SUPPORT_SLE_TREE_LEAF_SAMPLE)
    const uint8_t payload[] = "AUTO-UP";
    if (CONFIG_SLE_TREE_TEST_AUTO_UPLINK_DST_NODE_ID == g_sle_tree_ctx.cfg.node_id) {
        sle_tree_uart_printf("%s leaf auto-up skipped: dst_node_id=%u equals local node_id\r\n",
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
 * @brief Close the scan collection window and choose the best relay parent.
 */
void sle_tree_leaf_role_tick(uint64_t now_ms)
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
}

/**
 * @brief Leaf only keeps relay advertisements as scored parent candidates.
 */
void sle_tree_leaf_handle_seek_result(sle_seek_result_info_t *result, const sle_tree_adv_info_t *adv_info)
{
    if (result == NULL || adv_info == NULL || adv_info->role != SLE_TREE_ROLE_RELAY) {
        return;
    }
    if (adv_info->depth >= SLE_TREE_MAX_DEPTH - 1U) {
        return;
    }
    sle_tree_store_candidate(&result->addr, adv_info, (int8_t)result->rssi);
}

#if defined(CONFIG_SAMPLE_SUPPORT_SLE_TREE_LEAF_SAMPLE)
static void sle_tree_test_entry(void)
{
    (void)sle_tree_test_leaf_init();
}

app_run(sle_tree_test_entry);
#endif
