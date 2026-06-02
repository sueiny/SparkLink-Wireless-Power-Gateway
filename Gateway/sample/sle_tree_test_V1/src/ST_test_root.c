/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test root role entry.
 */
#include "app_init.h"
#include "common_def.h"
#include "securec.h"
#include "ST_test_internal.h"

/*--------------------------------------------------------------------------
 * Root role forwarding logic
 *--------------------------------------------------------------------------*/

/**
 * @brief Handle traffic uploaded from child relay/leaf into root.
 */
void sle_tree_root_handle_frame_from_child(uint16_t conn_id, const sle_tree_frame_view_t *frame,
    const uint8_t *data, uint16_t data_len)
{
    sle_tree_route_t *route;
    uint8_t forward_buf[SLE_TREE_MAX_FRAME_LEN] = {0};

    if (frame == NULL) {
        return;
    }
    if (frame->frame_type == SLE_TREE_FRAME_TYPE_TOPO_SUMMARY && frame->dst_node_id == g_sle_tree_ctx.cfg.node_id) {
        if (sle_tree_root_process_topo_summary(frame->payload, frame->payload_len)) {
            sle_tree_root_print_topology_tree();
        }
        return;
    }
    if (frame->frame_type == SLE_TREE_FRAME_TYPE_HEARTBEAT || frame->dst_node_id == g_sle_tree_ctx.cfg.node_id) {
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
    sle_tree_report_unreachable(frame->dst_node_id);
}

/**
 * @brief Root has no parent, so parent-side frame is always unexpected.
 */
void sle_tree_root_handle_frame_from_parent(const sle_tree_frame_view_t *frame)
{
    if (frame != NULL && frame->dst_node_id != g_sle_tree_ctx.cfg.node_id && frame->dst_node_id != SLE_TREE_ANY_NODE_ID) {
        sle_tree_report_unreachable(frame->dst_node_id);
    }
}

/**
 * @brief Root never owns uplink parent connection.
 */
bool sle_tree_root_is_uplink_addr(const sle_addr_t *addr)
{
    unused(addr);
    return false;
}

/**
 * @brief Root auto test sends one downlink packet to the first known tree node.
 */
void sle_tree_root_try_send_auto_traffic(void)
{
#if defined(CONFIG_SLE_TREE_TEST_ENABLE_AUTO_TRAFFIC)
    sle_tree_route_t *route = sle_tree_pick_first_node_route();

    if (route != NULL) {
        const uint8_t payload[] = "AUTO-DOWN";
        sle_tree_send_local_data(route->node_id, payload, sizeof(payload) - 1U);
    }
#endif
}

/**
 * @brief Root currently has no extra per-loop role maintenance.
 */
void sle_tree_root_role_tick(uint64_t now_ms)
{
    unused(now_ms);
    sle_tree_root_remove_stale_topology();
}

#if defined(CONFIG_SAMPLE_SUPPORT_SLE_TREE_ROOT_SAMPLE)
static void sle_tree_test_entry(void)
{
    (void)sle_tree_test_root_init();
}

app_run(sle_tree_test_entry);
#endif
