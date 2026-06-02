/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test demo sample main flow.
 */
#include "ST_test_internal.h"

#include <string.h>

#include "common_def.h"
#include "securec.h"
#include "sle_errcode.h"
#include "sle_ssap_client.h"
#include "sle_ssap_server.h"
#include "soc_osal.h"
#include "systick.h"
#include "watchdog.h"

sle_tree_ctx_t g_sle_tree_ctx;

const uint8_t g_sle_tree_uuid_base[SLE_UUID_LEN] = {
    0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
    0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*--------------------------------------------------------------------------
 * Common runtime helpers
 * 公共运行时辅助
 *--------------------------------------------------------------------------*/

/**
 * @brief Return current systick in milliseconds.
 * @brief 返回当前毫秒级 systick 计数。
 */
uint64_t sle_tree_now_ms(void)
{
    return uapi_systick_get_ms();
}

/**
 * @brief Compare two SLE addresses by raw MAC bytes only.
 * @brief 仅按 MAC 字节比较两个 SLE 地址，忽略地址类型字段差异。
 */
bool sle_tree_addr_equal(const sle_addr_t *left, const sle_addr_t *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    return (memcmp(left->addr, right->addr, SLE_ADDR_LEN) == 0);
}

/**
 * @brief Check whether current role exposes server-side behavior.
 * @brief 判断当前角色是否具备 server 侧能力。
 */
bool sle_tree_role_has_server(void)
{
    return (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT || g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY);
}

/**
 * @brief Check whether current role exposes client-side behavior.
 * @brief 判断当前角色是否具备 client 侧能力。
 */
bool sle_tree_role_has_client(void)
{
    return (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY || g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF);
}

/*--------------------------------------------------------------------------
 * Common frame dispatch
 * 通用帧分发
 *--------------------------------------------------------------------------*/

/**
 * @brief Parse and dispatch one frame received from a direct child.
 * @brief 解析并分发来自直连子节点的一帧数据。
 */
void sle_tree_handle_frame_from_child(uint16_t conn_id, const uint8_t *data, uint16_t data_len)
{
    sle_tree_frame_view_t frame = {0};

    if (!sle_tree_parse_frame(data, data_len, &frame)) {
        sle_tree_uart_printf("%s frame parse FAIL conn=%u len=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, data_len);
        return;
    }
    if (frame.frame_type != SLE_TREE_FRAME_TYPE_DATA) {
        sle_tree_uart_printf("%s frame recv conn=%u type=%u src=%u dst=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, frame.frame_type, frame.src_node_id, frame.dst_node_id);
    }
    if (sle_tree_mark_child_identity(conn_id, frame.src_node_id, frame.src_role)) {
        sle_tree_send_topo_summary();
    }
    sle_tree_learn_route(frame.src_node_id, frame.src_role, conn_id);
    sle_tree_root_touch_route_activity(frame.src_node_id, frame.src_role, conn_id);

    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        sle_tree_root_refresh_topo_activity(frame.src_node_id);
        /* 同时刷新转发 relay 的拓扑活动，防止 relay 只转发不发帧导致拓扑超时 */
        {
            sle_tree_child_link_t *fwd_child = sle_tree_find_child_by_conn(conn_id);
            if (fwd_child != NULL && fwd_child->direct_node_id != SLE_TREE_INVALID_NODE_ID) {
                sle_tree_root_refresh_topo_activity(fwd_child->direct_node_id);
            }
        }
        sle_tree_root_handle_frame_from_child(conn_id, &frame, data, data_len);
        return;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        sle_tree_relay_handle_frame_from_child(conn_id, &frame, data, data_len);
        return;
    }
    sle_tree_report_unreachable(frame.dst_node_id);
}

/**
 * @brief Parse and dispatch one frame received from current uplink parent.
 * @brief 解析并分发来自当前父节点的一帧数据。
 */
void sle_tree_handle_frame_from_parent(const uint8_t *data, uint16_t data_len)
{
    sle_tree_frame_view_t frame = {0};

    if (!sle_tree_parse_frame(data, data_len, &frame)) {
        return;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        sle_tree_relay_handle_frame_from_parent(&frame, data, data_len);
        return;
    }
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF) {
        sle_tree_leaf_handle_frame_from_parent(&frame);
        return;
    }
    sle_tree_root_handle_frame_from_parent(&frame);
}

/**
 * @brief Trigger role-specific auto test traffic when feature is enabled.
 * @brief 在开启自动测试时触发角色专属的自动流量。
 */
void sle_tree_try_send_auto_traffic(void)
{
#if defined(CONFIG_SLE_TREE_TEST_ENABLE_AUTO_TRAFFIC)
    uint64_t now_ms = sle_tree_now_ms();

    if ((now_ms - g_sle_tree_ctx.last_auto_traffic_ms) < CONFIG_SLE_TREE_TEST_AUTO_PERIOD_MS) {
        return;
    }
    g_sle_tree_ctx.last_auto_traffic_ms = now_ms;
    if (g_sle_tree_ctx.role == SLE_TREE_ROLE_ROOT) {
        sle_tree_root_try_send_auto_traffic();
    } else if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
        sle_tree_relay_try_send_auto_traffic();
    } else if (g_sle_tree_ctx.role == SLE_TREE_ROLE_LEAF) {
        sle_tree_leaf_try_send_auto_traffic();
    }
#endif
}

/**
 * @brief Main worker loop: UART, route aging, rescan, heartbeat and auto traffic.
 * @brief 主工作循环：处理 UART、路由老化、重扫、心跳和自动测试流量。
 */
static void sle_tree_worker(void)
{
    for (;;) {
        uint64_t now_ms = sle_tree_now_ms();

        sle_tree_handle_uart_input();
        sle_tree_remove_stale_routes();
        if (sle_tree_role_has_client() && g_sle_tree_ctx.start_scan_pending &&
            now_ms >= g_sle_tree_ctx.rescan_due_ms && !g_sle_tree_ctx.uplink.connected && !g_sle_tree_ctx.seeking &&
            !g_sle_tree_ctx.pending_parent.valid) {
            sle_tree_start_scan();
        }
        if (sle_tree_role_has_client() && g_sle_tree_ctx.pending_parent.valid && !g_sle_tree_ctx.uplink.connected &&
            !g_sle_tree_ctx.seeking && g_sle_tree_ctx.parent_connect_start_ms != 0 &&
            (now_ms - g_sle_tree_ctx.parent_connect_start_ms) >= SLE_TREE_CONNECT_CALLBACK_TIMEOUT_MS) {
            sle_tree_mark_parent_connect_failed(g_sle_tree_ctx.pending_parent.node_id);
            sle_tree_uart_printf("%s parent connect timeout node=%u age=%u fail_count=%u\r\n",
                SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.pending_parent.node_id,
                (uint32_t)(now_ms - g_sle_tree_ctx.parent_connect_start_ms),
                sle_tree_get_parent_fail_count(g_sle_tree_ctx.pending_parent.node_id));
            (void)sle_remove_paired_remote_device(&g_sle_tree_ctx.pending_parent.addr);
            (void)memset_s(&g_sle_tree_ctx.pending_parent, sizeof(g_sle_tree_ctx.pending_parent), 0,
                sizeof(g_sle_tree_ctx.pending_parent));
            g_sle_tree_ctx.parent_connect_start_ms = 0;
            sle_tree_schedule_rescan(SLE_TREE_RESCAN_DELAY_MS);
        }
        sle_tree_role_tick(now_ms);
#if !defined(CONFIG_SLE_TREE_TEST_DISABLE_HEARTBEAT)
        if (sle_tree_role_has_client() && g_sle_tree_ctx.uplink.connected && g_sle_tree_ctx.uplink.handle_ready &&
            (now_ms - g_sle_tree_ctx.last_heartbeat_ms) >= SLE_TREE_HEARTBEAT_PERIOD_MS) {
            g_sle_tree_ctx.last_heartbeat_ms = now_ms;
            sle_tree_send_heartbeat();
        }
#endif
        sle_tree_try_send_auto_traffic();
        if ((now_ms - g_sle_tree_ctx.last_loss_report_ms) >= SLE_TREE_LOSS_REPORT_PERIOD_MS) {
            g_sle_tree_ctx.last_loss_report_ms = now_ms;
            sle_tree_loss_report();
        }
        osal_msleep(50);
    }
}

/*--------------------------------------------------------------------------
 * SSAP server callbacks
 * SSAP server 回调
 *--------------------------------------------------------------------------*/

static void sle_tree_ssaps_read_cb(uint8_t server_id, uint16_t conn_id, ssaps_req_read_cb_t *read_cb, errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(read_cb);
    unused(status);
}

/**
 * @brief Handle write request from child side and forward into tree dispatcher.
 * @brief 处理子节点写请求，并交给树状网络分发逻辑。
 */
static void sle_tree_ssaps_write_cb(uint8_t server_id, uint16_t conn_id, ssaps_req_write_cb_t *write_cb, errcode_t status)
{
    ssaps_send_rsp_t rsp = {0};

    unused(server_id);
    unused(status);
    if (write_cb == NULL) {
        return;
    }
    if (write_cb->need_rsp) {
        rsp.request_id = write_cb->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        rsp.value_len = 0;
        rsp.value = NULL;
        (void)ssaps_send_response(g_sle_tree_ctx.server_id, conn_id, &rsp);
    }
    sle_tree_handle_frame_from_child(conn_id, write_cb->value, write_cb->length);
}

static void sle_tree_ssaps_mtu_changed_cb(uint8_t server_id, uint16_t conn_id, ssap_exchange_info_t *info,
    errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(info);
    unused(status);
}

static void sle_tree_ssaps_start_service_cb(uint8_t server_id, uint16_t handle, errcode_t status)
{
    unused(server_id);
    unused(handle);
    unused(status);
}

static void sle_tree_register_server_callbacks(void)
{
    ssaps_callbacks_t callbacks = {0};

    callbacks.start_service_cb = sle_tree_ssaps_start_service_cb;
    callbacks.mtu_changed_cb = sle_tree_ssaps_mtu_changed_cb;
    callbacks.read_request_cb = sle_tree_ssaps_read_cb;
    callbacks.write_request_cb = sle_tree_ssaps_write_cb;
    (void)ssaps_register_callbacks(&callbacks);
}

/**
 * @brief Create the demo private service/property used for tree data transport.
 * @brief 创建 demo 的私有 service/property，用于树状网络数据承载。
 */
static errcode_t sle_tree_server_init(void)
{
    uint8_t init_value[2] = {0};
    uint8_t desc_value[2] = {0x01, 0x00};
    sle_uuid_t app_uuid = {0};
    sle_uuid_t service_uuid = {0};
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};

    app_uuid.len = 2;
    app_uuid.uuid[0] = (uint8_t)(SLE_TREE_APP_UUID & 0xFFU);
    app_uuid.uuid[1] = (uint8_t)((SLE_TREE_APP_UUID >> 8U) & 0xFFU);
    if (ssaps_register_server(&app_uuid, &g_sle_tree_ctx.server_id) != ERRCODE_SUCC) {
        return ERRCODE_FAIL;
    }
    sle_tree_uuid_set_u16(SLE_TREE_SERVICE_UUID, &service_uuid);
    if (ssaps_add_service_sync(g_sle_tree_ctx.server_id, &service_uuid, 1, &g_sle_tree_ctx.service_handle) != ERRCODE_SUCC) {
        return ERRCODE_FAIL;
    }

    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
        SSAP_OPERATE_INDICATION_BIT_WRITE | SSAP_OPERATE_INDICATION_BIT_NOTIFY;
    sle_tree_uuid_set_u16(SLE_TREE_PROPERTY_UUID, &property.uuid);
    property.value_len = sizeof(init_value);
    property.value = init_value;
    if (ssaps_add_property_sync(g_sle_tree_ctx.server_id, g_sle_tree_ctx.service_handle, &property,
        &g_sle_tree_ctx.property_handle) != ERRCODE_SUCC) {
        return ERRCODE_FAIL;
    }

    descriptor.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
        SSAP_OPERATE_INDICATION_BIT_WRITE |
        SSAP_OPERATE_INDICATION_BIT_DESCRIPTOR_CLIENT_CONFIGURATION_WRITE;
    descriptor.type = SSAP_DESCRIPTOR_USER_DESCRIPTION;
    descriptor.value_len = sizeof(desc_value);
    descriptor.value = desc_value;
    if (ssaps_add_descriptor_sync(g_sle_tree_ctx.server_id, g_sle_tree_ctx.service_handle,
        g_sle_tree_ctx.property_handle, &descriptor) != ERRCODE_SUCC) {
        return ERRCODE_FAIL;
    }

    if (ssaps_start_service(g_sle_tree_ctx.server_id, g_sle_tree_ctx.service_handle) != ERRCODE_SUCC) {
        return ERRCODE_FAIL;
    }

    {
        ssap_exchange_info_t info = {0};
        info.mtu_size = SLE_TREE_MTU_SIZE;
        info.version = 1;
        (void)ssaps_set_info(g_sle_tree_ctx.server_id, &info);
    }
    g_sle_tree_ctx.server_ready = true;
    return ERRCODE_SUCC;
}

/*--------------------------------------------------------------------------
 * SSAP client callbacks
 * SSAP client 回调
 *--------------------------------------------------------------------------*/

static void sle_tree_note_uplink_setup_failed(const sle_addr_t *addr, const char *reason);

static void sle_tree_ssapc_notification_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *value,
    errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(status);
    if (value == NULL) {
        return;
    }
    sle_tree_handle_frame_from_parent(value->data, value->data_len);
}

static void sle_tree_ssapc_indication_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *value,
    errcode_t status)
{
    sle_tree_ssapc_notification_cb(client_id, conn_id, value, status);
}

static void sle_tree_ssapc_exchange_info_cb(uint8_t client_id, uint16_t conn_id, ssap_exchange_info_t *info,
    errcode_t status)
{
    ssapc_find_structure_param_t find_param = {0};
    errcode_t ret;

    unused(client_id);
    unused(info);
    if (status != ERRCODE_SUCC) {
        sle_tree_uart_printf("%s uplink exchange failed conn=%u status=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, status);
        if (g_sle_tree_ctx.uplink.connected && conn_id == g_sle_tree_ctx.uplink.conn_id) {
            sle_tree_note_uplink_setup_failed(&g_sle_tree_ctx.uplink.addr, "exchange");
            sle_tree_handle_uplink_disconnected();
        }
        return;
    }
    find_param.type = SSAP_FIND_TYPE_PROPERTY;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    ret = ssapc_find_structure(SLE_TREE_CLIENT_ID, conn_id, &find_param);
    sle_tree_uart_printf("%s uplink find structure conn=%u ret=0x%X\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, conn_id, ret);
    if (ret != ERRCODE_SUCC && g_sle_tree_ctx.uplink.connected && conn_id == g_sle_tree_ctx.uplink.conn_id) {
        sle_tree_note_uplink_setup_failed(&g_sle_tree_ctx.uplink.addr, "find_req");
        sle_tree_handle_uplink_disconnected();
    }
}

static void sle_tree_ssapc_find_structure_cb(uint8_t client_id, uint16_t conn_id,
    ssapc_find_service_result_t *service, errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(service);
    unused(status);
}

static void sle_tree_ssapc_find_structure_cmp_cb(uint8_t client_id, uint16_t conn_id,
    ssapc_find_structure_result_t *result, errcode_t status)
{
    unused(client_id);
    unused(result);
    if (status != ERRCODE_SUCC) {
        sle_tree_uart_printf("%s uplink find structure cmp failed conn=%u status=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, status);
        if (g_sle_tree_ctx.uplink.connected && conn_id == g_sle_tree_ctx.uplink.conn_id) {
            sle_tree_note_uplink_setup_failed(&g_sle_tree_ctx.uplink.addr, "find_cmp");
            sle_tree_handle_uplink_disconnected();
        }
    }
}

static void sle_tree_ssapc_find_property_cb(uint8_t client_id, uint16_t conn_id, ssapc_find_property_result_t *property,
    errcode_t status)
{
    unused(client_id);
    if (status != ERRCODE_SUCC) {
        sle_tree_uart_printf("%s uplink find property failed conn=%u status=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, status);
        return;
    }
    if (property == NULL || conn_id != g_sle_tree_ctx.uplink.conn_id) {
        return;
    }
    if ((property->uuid.len == SLE_UUID_LEN &&
        property->uuid.uuid[12] == (uint8_t)(SLE_TREE_PROPERTY_UUID & 0xFFU) &&
        property->uuid.uuid[13] == (uint8_t)((SLE_TREE_PROPERTY_UUID >> 8U) & 0xFFU)) ||
        (property->uuid.len == 2 && sle_tree_get_le16(&property->uuid.uuid[14]) == SLE_TREE_PROPERTY_UUID)) {
        g_sle_tree_ctx.uplink.write_handle = property->handle;
        g_sle_tree_ctx.uplink.handle_ready = true;
        g_sle_tree_ctx.uplink.connected_at_ms = sle_tree_now_ms();
        sle_tree_uart_printf("%s uplink handle ready conn=%u handle=%u uuid=0x%04X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, property->handle, SLE_TREE_PROPERTY_UUID);
        if (g_sle_tree_ctx.pending_parent.valid) {
            g_sle_tree_ctx.uplink.parent_node_id = g_sle_tree_ctx.pending_parent.node_id;
            g_sle_tree_ctx.uplink.parent_role = g_sle_tree_ctx.pending_parent.role;
            g_sle_tree_ctx.uplink.root_node_id = g_sle_tree_ctx.pending_parent.root_node_id;
            g_sle_tree_ctx.uplink.depth = (uint8_t)(g_sle_tree_ctx.pending_parent.depth + 1U);
            g_sle_tree_ctx.uplink.parent_free_slots = g_sle_tree_ctx.pending_parent.free_slots;
            g_sle_tree_ctx.uplink.parent_rssi = g_sle_tree_ctx.pending_parent.rssi;
            g_sle_tree_ctx.cfg.last_parent_node_id = g_sle_tree_ctx.pending_parent.node_id;
            sle_tree_uart_printf("%s attach self=%u parent=%u parent_role=%s root=%u depth=%u rssi=%d free_slots=%u\r\n",
                SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.cfg.node_id, g_sle_tree_ctx.uplink.parent_node_id,
                sle_tree_role_name(g_sle_tree_ctx.uplink.parent_role), g_sle_tree_ctx.uplink.root_node_id,
                g_sle_tree_ctx.uplink.depth, g_sle_tree_ctx.uplink.parent_rssi, g_sle_tree_ctx.uplink.parent_free_slots);
            sle_tree_reset_parent_fail_count(g_sle_tree_ctx.uplink.parent_node_id);
            sle_tree_reset_rescan_count();  /* 连接成功，重置退避计数 */
            sle_tree_save_cfg();
            (void)memset_s(&g_sle_tree_ctx.pending_parent, sizeof(g_sle_tree_ctx.pending_parent), 0,
                sizeof(g_sle_tree_ctx.pending_parent));
        }
        sle_tree_refresh_advertising();
        if (g_sle_tree_ctx.role == SLE_TREE_ROLE_RELAY) {
            g_sle_tree_ctx.last_heartbeat_ms = sle_tree_now_ms();
            g_sle_tree_ctx.last_topo_summary_ms = g_sle_tree_ctx.last_heartbeat_ms;
#if !defined(CONFIG_SLE_TREE_TEST_DISABLE_HEARTBEAT)
            sle_tree_send_heartbeat();
#endif
            sle_tree_send_topo_summary();
        }
    }
}

static void sle_tree_ssapc_write_cfm_cb(uint8_t client_id, uint16_t conn_id, ssapc_write_result_t *result,
    errcode_t status)
{
    unused(client_id);
    unused(result);
    if (status != ERRCODE_SUCC) {
        sle_tree_uart_printf("%s uplink write cfm failed conn=%u status=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, conn_id, status);
        if (g_sle_tree_ctx.uplink.connected && conn_id == g_sle_tree_ctx.uplink.conn_id) {
            sle_tree_handle_uplink_disconnected();
        }
    }
}

static void sle_tree_register_client_callbacks(void)
{
    ssapc_callbacks_t callbacks = {0};

    callbacks.exchange_info_cb = sle_tree_ssapc_exchange_info_cb;
    callbacks.find_structure_cb = sle_tree_ssapc_find_structure_cb;
    callbacks.find_structure_cmp_cb = sle_tree_ssapc_find_structure_cmp_cb;
    callbacks.ssapc_find_property_cbk = sle_tree_ssapc_find_property_cb;
    callbacks.write_cfm_cb = sle_tree_ssapc_write_cfm_cb;
    callbacks.notification_cb = sle_tree_ssapc_notification_cb;
    callbacks.indication_cb = sle_tree_ssapc_indication_cb;
    (void)ssapc_register_callbacks(&callbacks);
}

/*--------------------------------------------------------------------------
 * SLE common callbacks
 * SLE 公共回调
 *--------------------------------------------------------------------------*/

static void sle_tree_seek_result_cb(sle_seek_result_info_t *result)
{
    sle_tree_adv_info_t adv_info = {0};

    if (result == NULL || result->data == NULL || !sle_tree_parse_adv_data(result->data, result->data_length, &adv_info)) {
        return;
    }
    sle_tree_handle_seek_result(result);
}

static void sle_tree_seek_enable_cb(errcode_t status)
{
    unused(status);
}

static void sle_tree_seek_disable_cb(errcode_t status)
{
    errcode_t ret;

    unused(status);
    g_sle_tree_ctx.seeking = false;
    if (g_sle_tree_ctx.optimize_scan_active) {
        g_sle_tree_ctx.optimize_scan_active = false;
        return;
    }
    if (g_sle_tree_ctx.pending_parent.valid) {
        ret = sle_connect_remote_device(&g_sle_tree_ctx.pending_parent.addr);
        sle_tree_uart_printf("%s connect parent node=%u ret=0x%X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.pending_parent.node_id, ret);
        if (ret == ERRCODE_SUCC) {
            g_sle_tree_ctx.parent_connect_start_ms = sle_tree_now_ms();
            return;
        }
        sle_tree_mark_parent_connect_failed(g_sle_tree_ctx.pending_parent.node_id);
        (void)memset_s(&g_sle_tree_ctx.pending_parent, sizeof(g_sle_tree_ctx.pending_parent), 0,
            sizeof(g_sle_tree_ctx.pending_parent));
        g_sle_tree_ctx.parent_connect_start_ms = 0;
        sle_tree_schedule_rescan(SLE_TREE_RESCAN_DELAY_MS);
        return;
    }
    if (g_sle_tree_ctx.uplink.connected) {
        return;
    }
    sle_tree_schedule_rescan(SLE_TREE_RESCAN_DELAY_MS);
}

static void sle_tree_announce_enable_cb(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
    unused(status);
}

static void sle_tree_announce_disable_cb(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
    unused(status);
}

static void sle_tree_announce_terminal_cb(uint32_t announce_id)
{
    unused(announce_id);
}

static uint16_t sle_tree_parent_node_id_from_state(const sle_addr_t *addr)
{
    uint16_t node_id;

    if (g_sle_tree_ctx.uplink.parent_node_id != SLE_TREE_INVALID_NODE_ID) {
        return g_sle_tree_ctx.uplink.parent_node_id;
    }
    if (g_sle_tree_ctx.pending_parent.valid) {
        return g_sle_tree_ctx.pending_parent.node_id;
    }
    node_id = sle_tree_node_id_from_addr(addr);
    if (node_id != SLE_TREE_INVALID_NODE_ID) {
        return node_id;
    }
    return g_sle_tree_ctx.cfg.last_parent_node_id;
}

static void sle_tree_note_uplink_setup_failed(const sle_addr_t *addr, const char *reason)
{
    uint16_t parent_node_id = sle_tree_parent_node_id_from_state(addr);

    if (parent_node_id != SLE_TREE_INVALID_NODE_ID && parent_node_id != SLE_TREE_INVALID_LAST_PARENT) {
        sle_tree_mark_parent_connect_failed(parent_node_id);
        sle_tree_uart_printf("%s uplink setup failed node=%u reason=%s fail_count=%u\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, parent_node_id, reason,
            sle_tree_get_parent_fail_count(parent_node_id));
    } else {
        sle_tree_uart_printf("%s uplink setup failed node=unknown reason=%s\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, reason);
    }
    if (addr != NULL) {
        (void)sle_remove_paired_remote_device(addr);
    }
}

static errcode_t sle_tree_start_uplink_exchange(uint16_t conn_id, const char *reason)
{
    ssap_exchange_info_t info = {0};
    errcode_t ret;

    info.mtu_size = SLE_TREE_MTU_SIZE;
    info.version = 1;
    ret = ssapc_exchange_info_req(SLE_TREE_CLIENT_ID, conn_id, &info);
    sle_tree_uart_printf("%s uplink exchange start conn=%u reason=%s ret=0x%X\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, conn_id, reason, ret);
    if (ret != ERRCODE_SUCC && g_sle_tree_ctx.uplink.connected && conn_id == g_sle_tree_ctx.uplink.conn_id) {
        sle_tree_note_uplink_setup_failed(&g_sle_tree_ctx.uplink.addr, "exchange_req");
        sle_tree_handle_uplink_disconnected();
    }
    return ret;
}

static void sle_tree_auth_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
    const sle_auth_info_evt_t *evt)
{
    unused(evt);
    sle_tree_uart_printf("%s auth complete conn=%u node=%u status=0x%X\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, conn_id, sle_tree_node_id_from_addr(addr), status);
    if (status == ERRCODE_SUCC) {
        return;
    }
    if (sle_tree_is_uplink_addr(addr)) {
        sle_tree_note_uplink_setup_failed(addr, "auth");
        sle_tree_handle_uplink_disconnected();
        return;
    }
    if (addr != NULL) {
        (void)sle_remove_paired_remote_device(addr);
    }
}

static void sle_tree_pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    sle_tree_uart_printf("%s pair complete conn=%u node=%u status=0x%X\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, conn_id, sle_tree_node_id_from_addr(addr), status);
    if (status != ERRCODE_SUCC) {
        if (sle_tree_is_uplink_addr(addr)) {
            sle_tree_note_uplink_setup_failed(addr, "pair");
            sle_tree_handle_uplink_disconnected();
            return;
        }
        if (addr != NULL) {
            (void)sle_remove_paired_remote_device(addr);
        }
        return;
    }
    if (sle_tree_is_uplink_addr(addr)) {
        (void)sle_tree_start_uplink_exchange(conn_id, "pair_done");
    }
}

static void sle_tree_connect_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr, sle_acb_state_t conn_state,
    sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    uint16_t node_id = sle_tree_node_id_from_addr(addr);

    sle_tree_uart_printf("%s conn state conn=%u node=%u state=%u pair=%u reason=0x%X\r\n",
        SLE_TREE_SERVER_LOG_PREFIX, conn_id, node_id, conn_state, pair_state, disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        if (sle_tree_role_has_client() && sle_tree_is_uplink_addr(addr)) {
            g_sle_tree_ctx.parent_connect_start_ms = 0;
            sle_tree_uart_printf("%s conn classify conn=%u node=%u as=uplink\r\n",
                SLE_TREE_SERVER_LOG_PREFIX, conn_id, node_id);
            sle_tree_handle_uplink_connected(conn_id, addr, pair_state);
            return;
        }
        if (sle_tree_role_has_server()) {
            sle_tree_uart_printf("%s conn classify conn=%u node=%u as=child\r\n",
                SLE_TREE_SERVER_LOG_PREFIX, conn_id, node_id);
            (void)sle_tree_alloc_child(conn_id, addr);
            sle_tree_refresh_advertising();
        }
        return;
    }

    if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        if (sle_tree_role_has_client() && !g_sle_tree_ctx.uplink.connected && g_sle_tree_ctx.pending_parent.valid &&
            addr != NULL && sle_tree_addr_equal(addr, &g_sle_tree_ctx.pending_parent.addr)) {
            sle_tree_mark_parent_connect_failed(g_sle_tree_ctx.pending_parent.node_id);
            sle_tree_uart_printf("%s parent connect failed node=%u fail_count=%u\r\n",
                SLE_TREE_SERVER_LOG_PREFIX, g_sle_tree_ctx.pending_parent.node_id,
                sle_tree_get_parent_fail_count(g_sle_tree_ctx.pending_parent.node_id));
            g_sle_tree_ctx.parent_connect_start_ms = 0;
            sle_tree_handle_uplink_disconnected();
            return;
        }
        if (sle_tree_role_has_client() && g_sle_tree_ctx.uplink.connected && conn_id == g_sle_tree_ctx.uplink.conn_id) {
            if (!g_sle_tree_ctx.uplink.handle_ready) {
                sle_tree_note_uplink_setup_failed(addr, "disconnect_before_ready");
            }
            sle_tree_handle_uplink_disconnected();
            return;
        }
        sle_tree_remove_child(conn_id);
        sle_tree_refresh_advertising();
    }
}

static void sle_tree_connect_update_req_cb(uint16_t conn_id, errcode_t status,
    const sle_connection_param_update_req_t *param)
{
    unused(conn_id);
    unused(status);
    unused(param);
}

static void sle_tree_connect_update_cb(uint16_t conn_id, errcode_t status,
    const sle_connection_param_update_evt_t *param)
{
    unused(conn_id);
    unused(status);
    unused(param);
}

static void sle_tree_read_rssi_cb(uint16_t conn_id, int8_t rssi, errcode_t status)
{
    unused(conn_id);
    unused(rssi);
    unused(status);
}

static void sle_tree_sle_enable_cb(errcode_t status)
{
    if (status != ERRCODE_SUCC) {
        return;
    }
    g_sle_tree_ctx.sle_enabled = true;
    sle_tree_set_local_addr_from_nv();
    if (sle_tree_role_has_server()) {
        if (sle_tree_server_init() == ERRCODE_SUCC) {
            sle_tree_refresh_advertising();
        }
    }
    if (sle_tree_role_has_client()) {
        /* 首次扫描随机延迟：100ms ~ 5100ms，避免52个节点上电后同时开始扫描 */
        uint32_t init_delay = 100 + (g_sle_tree_ctx.cfg.node_id % 5000);
        sle_tree_connect_param_init();
        sle_tree_schedule_rescan(init_delay);
    }
}

static void sle_tree_register_common_callbacks(void)
{
    sle_announce_seek_callbacks_t seek_callbacks = {0};
    sle_connection_callbacks_t conn_callbacks = {0};

    seek_callbacks.sle_enable_cb = sle_tree_sle_enable_cb;
    seek_callbacks.seek_enable_cb = sle_tree_seek_enable_cb;
    seek_callbacks.seek_disable_cb = sle_tree_seek_disable_cb;
    seek_callbacks.seek_result_cb = sle_tree_seek_result_cb;
    seek_callbacks.announce_enable_cb = sle_tree_announce_enable_cb;
    seek_callbacks.announce_disable_cb = sle_tree_announce_disable_cb;
    seek_callbacks.announce_terminal_cb = sle_tree_announce_terminal_cb;
    (void)sle_announce_seek_register_callbacks(&seek_callbacks);

    conn_callbacks.connect_state_changed_cb = sle_tree_connect_state_changed_cb;
    conn_callbacks.auth_complete_cb = sle_tree_auth_complete_cb;
    conn_callbacks.pair_complete_cb = sle_tree_pair_complete_cb;
    conn_callbacks.connect_param_update_req_cb = sle_tree_connect_update_req_cb;
    conn_callbacks.connect_param_update_cb = sle_tree_connect_update_cb;
    conn_callbacks.read_rssi_cb = sle_tree_read_rssi_cb;
    (void)sle_connection_register_callbacks(&conn_callbacks);
}

/*--------------------------------------------------------------------------
 * Task entry and public init
 * 任务入口与公开初始化接口
 *--------------------------------------------------------------------------*/

/**
 * @brief Main sample task: load config, register callbacks, enable SLE and run worker loop.
 * @brief sample 主任务：加载配置、注册回调、使能 SLE，并进入工作循环。
 */
static int sle_tree_main_task(void)
{
    char local_name[SLE_TREE_NAME_MAX_LEN + 1] = {0};
    char parent_name[SLE_TREE_NAME_MAX_LEN + 1] = {0};
    sle_addr_t local_sle_addr = {0};

    uapi_watchdog_disable();
    sle_tree_load_cfg();
    sle_tree_get_cfg_name(g_sle_tree_ctx.cfg.local_name, local_name, sizeof(local_name));
    sle_tree_get_cfg_name(g_sle_tree_ctx.cfg.parent_name, parent_name, sizeof(parent_name));
    if (sle_tree_get_factory_sle_addr(&local_sle_addr)) {
        sle_tree_uart_printf(
            "%s role=%s node_id=%u last_parent=%u local_name=%s parent_name=%s sle_mac=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, sle_tree_role_name(g_sle_tree_ctx.role), g_sle_tree_ctx.cfg.node_id,
            g_sle_tree_ctx.cfg.last_parent_node_id, local_name, parent_name,
            local_sle_addr.addr[0], local_sle_addr.addr[1], local_sle_addr.addr[2],
            local_sle_addr.addr[3], local_sle_addr.addr[4], local_sle_addr.addr[5]);
    } else {
        sle_tree_uart_printf("%s role=%s node_id=%u last_parent=%u local_name=%s parent_name=%s sle_mac=unknown\r\n",
            SLE_TREE_SERVER_LOG_PREFIX, sle_tree_role_name(g_sle_tree_ctx.role), g_sle_tree_ctx.cfg.node_id,
            g_sle_tree_ctx.cfg.last_parent_node_id, local_name, parent_name);
    }
    sle_tree_uart_register_rx_callback();

    sle_tree_register_common_callbacks();
    if (sle_tree_role_has_server()) {
        sle_tree_register_server_callbacks();
    }
    if (sle_tree_role_has_client()) {
        sle_tree_register_client_callbacks();
    }
    (void)enable_sle();
    sle_tree_worker();
    return 0;
}

/**
 * @brief Start tree demo task with explicit runtime role.
 * @brief 根据指定角色启动树状网络 demo 任务。
 */
errcode_t sle_tree_test_init_with_role(sle_tree_role_t role)
{
    osal_task *task = NULL;

    (void)memset_s(&g_sle_tree_ctx, sizeof(g_sle_tree_ctx), 0, sizeof(g_sle_tree_ctx));
    g_sle_tree_ctx.role = (uint8_t)role;
    g_sle_tree_ctx.uplink.conn_id = SLE_TREE_INVALID_CONN_ID;
    g_sle_tree_ctx.rescan_due_ms = 0;
    g_sle_tree_ctx.cfg.last_parent_node_id = SLE_TREE_INVALID_LAST_PARENT;
    g_sle_tree_ctx.last_loss_report_ms = sle_tree_now_ms();
    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)sle_tree_main_task, NULL, "SleTreeTask", SLE_TREE_TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, SLE_TREE_TASK_PRIO);
        osal_kfree(task);
    }
    osal_kthread_unlock();
    return ERRCODE_SUCC;
}

/**
 * @brief Convenience init wrapper for root role build.
 * @brief root 角色的快捷初始化封装。
 */
errcode_t sle_tree_test_root_init(void)
{
    return sle_tree_test_init_with_role(SLE_TREE_ROLE_ROOT);
}

/**
 * @brief Convenience init wrapper for relay role build.
 * @brief relay 角色的快捷初始化封装。
 */
errcode_t sle_tree_test_relay_init(void)
{
    return sle_tree_test_init_with_role(SLE_TREE_ROLE_RELAY);
}

/**
 * @brief Convenience init wrapper for leaf role build.
 * @brief leaf 角色的快捷初始化封装。
 */
errcode_t sle_tree_test_leaf_init(void)
{
    return sle_tree_test_init_with_role(SLE_TREE_ROLE_LEAF);
}
