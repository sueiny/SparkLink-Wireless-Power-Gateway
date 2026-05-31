/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE mesh main init, callbacks and worker loop.
 */
#include "SM_mesh_internal.h"

#include <string.h>
#include <stdarg.h>

#include "app_init.h"
#include "common_def.h"
#include "securec.h"
#include "sle_errcode.h"
#include "soc_osal.h"
#include "systick.h"
#include "watchdog.h"

sm_ctx_t g_sm_ctx;

/*--------------------------------------------------------------------------
 * Common helpers / 公共辅助
 *--------------------------------------------------------------------------*/

uint64_t sm_now_ms(void)
{
    return uapi_systick_get_ms();
}

static void sm_reset_context(void)
{
    (void)memset_s(&g_sm_ctx, sizeof(g_sm_ctx), 0, sizeof(g_sm_ctx));
    g_sm_ctx.local_addr = SM_NODE_ADDR_INVALID;
}

/*--------------------------------------------------------------------------
 * Set local SLE address from NV / 从 NV 设置本机 SLE 地址
 *--------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 * SLE callbacks / SLE 回调
 *--------------------------------------------------------------------------*/

static void sm_sle_enable_cb(errcode_t status)
{
    if (status != ERRCODE_SUCC) {
        return;
    }
    g_sm_ctx.sle_enabled = true;
    sm_start_scan();
}

static void sm_announce_enable_cb(uint32_t announce_id, errcode_t status)
{
    if (status == ERRCODE_SUCC) {
        g_sm_ctx.announce_started = true;
    }
}

static void sm_announce_disable_cb(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
    unused(status);
    g_sm_ctx.announce_started = false;
}

static void sm_seek_enable_cb(errcode_t status)
{
    unused(status);
}

static void sm_seek_disable_cb(errcode_t status)
{
    unused(status);
}

static void sm_seek_result_cb(sle_seek_result_info_t *result)
{
    if (result == NULL) {
        return;
    }
    sm_handle_seek_result(result);
}

static void sm_register_callbacks(void)
{
    sle_announce_seek_callbacks_t seek_cb = {0};
    seek_cb.sle_enable_cb = sm_sle_enable_cb;
    seek_cb.announce_enable_cb = sm_announce_enable_cb;
    seek_cb.announce_disable_cb = sm_announce_disable_cb;
    seek_cb.seek_enable_cb = sm_seek_enable_cb;
    seek_cb.seek_disable_cb = sm_seek_disable_cb;
    seek_cb.seek_result_cb = sm_seek_result_cb;
    (void)sle_announce_seek_register_callbacks(&seek_cb);
}

/*--------------------------------------------------------------------------
 * Advertising init (before enable_sle) / 广播初始化（enable_sle 之前）
 *--------------------------------------------------------------------------*/

static void sm_adv_init(void)
{
    sle_announce_data_t announce_data = {0};
    uint8_t frame[SM_MAX_FRAME_LEN];
    uint8_t adv_buf[SM_ADV_DATA_LEN_MAX] = {0};
    uint8_t scan_rsp_buf[SM_ADV_DATA_LEN_MAX] = {0};
    uint16_t frame_len;
    uint16_t adv_len;
    uint16_t scan_rsp_len;

    sm_set_announce_params();

    /* Build initial RSSI report (empty neighbor list) */
    frame_len = sm_build_rssi_report(frame, sizeof(frame), g_sm_ctx.local_addr);
    sm_build_adv_data_direct(adv_buf, &adv_len, frame, frame_len);
    sm_build_scan_rsp(scan_rsp_buf, &scan_rsp_len);
    announce_data.announce_data = adv_buf;
    announce_data.announce_data_len = adv_len;
    announce_data.seek_rsp_data = scan_rsp_buf;
    announce_data.seek_rsp_data_len = scan_rsp_len;
    (void)sle_set_announce_data(SM_ADV_HANDLE, &announce_data);
    (void)sle_start_announce(SM_ADV_HANDLE);
}

/*--------------------------------------------------------------------------
 * Worker loop / 工作循环
 *--------------------------------------------------------------------------*/

static void sm_worker_loop(void)
{
    uint64_t now_ms;
    uint32_t next_rssi_ms = 0;

    for (;;) {
        now_ms = sm_now_ms();

        sm_age_neighbors();

        if (g_sm_ctx.is_dongle) {
            sm_uart_handle_input();
            sm_dongle_role_tick();
        }

        /* Advertising window: stop after duration / 广播窗口：超时后停止 */
        if (g_sm_ctx.announce_started && g_sm_ctx.adv_started_ms > 0) {
            if (((uint32_t)now_ms - g_sm_ctx.adv_started_ms) >= SM_ADV_DURATION_MS) {
                (void)sle_stop_announce(SM_ADV_HANDLE);
                g_sm_ctx.announce_started = false;
                g_sm_ctx.adv_started_ms = 0;
            }
        }

        /* If idle and relay queue has data, start sending / 空闲且队列有数据，开始发送 */
        if (!g_sm_ctx.announce_started && g_sm_ctx.relay_head != g_sm_ctx.relay_tail) {
            sm_refresh_advertising(g_sm_ctx.relay_queue[g_sm_ctx.relay_head],
                g_sm_ctx.relay_lens[g_sm_ctx.relay_head]);
            g_sm_ctx.adv_started_ms = (uint32_t)now_ms;
            g_sm_ctx.relay_head = (g_sm_ctx.relay_head + 1) % SM_RELAY_QUEUE_SIZE;
        }

        /* Non-dongle: periodic RSSI report / 非 dongle：周期性 RSSI 上报 */
        if (!g_sm_ctx.is_dongle) {
            if (!g_sm_ctx.announce_started && g_sm_ctx.relay_head == g_sm_ctx.relay_tail &&
                now_ms >= next_rssi_ms) {
                sm_node_send_rssi_report();
                g_sm_ctx.adv_started_ms = (uint32_t)now_ms;
                next_rssi_ms = (uint32_t)now_ms + SM_RSSI_REPORT_PERIOD_MS;
            }
        }

        osal_msleep(SM_WORKER_SLEEP_MS);
    }
}

/*--------------------------------------------------------------------------
 * Main task / 主任务
 *--------------------------------------------------------------------------*/

static int sm_main_task(void)
{
    uapi_watchdog_disable();

    if (g_sm_ctx.is_dongle) {
        sm_uart_register_rx();
    }

    sm_register_callbacks();
    sm_adv_init();
    (void)enable_sle();
    sm_worker_loop();
    return 0;
}

/*--------------------------------------------------------------------------
 * Public init / 公开初始化
 *--------------------------------------------------------------------------*/

static errcode_t sm_mesh_init_common(uint8_t addr, bool is_dongle)
{
    osal_task *task = NULL;

    sm_reset_context();
    g_sm_ctx.local_addr = addr;
    g_sm_ctx.is_dongle = is_dongle;

    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)sm_main_task, NULL, "SleMeshTask", SM_TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, SM_TASK_PRIO);
        osal_kfree(task);
    }
    osal_kthread_unlock();
    return ERRCODE_SUCC;
}

errcode_t sm_mesh_node_init(void)
{
#if defined(CONFIG_SAMPLE_SUPPORT_SLE_MESH_NODE_SAMPLE)
    return sm_mesh_init_common((uint8_t)CONFIG_SLE_MESH_NODE_ADDR, false);
#else
    return ERRCODE_FAIL;
#endif
}

errcode_t sm_mesh_dongle_init(void)
{
    return sm_mesh_init_common(SM_DONGLE_ADDR, true);
}

/*--------------------------------------------------------------------------
 * Node app_run entry / 节点应用入口
 *--------------------------------------------------------------------------*/

#if defined(CONFIG_SAMPLE_SUPPORT_SLE_MESH_NODE_SAMPLE)
static void sle_mesh_node_entry(void)
{
    (void)sm_mesh_node_init();
}

app_run(sle_mesh_node_entry);
#endif
