/**
 * @file dtu_service.c
 * @brief DTU manager：初始化顺序、输入分流和统一回包出口。
 */

#include "dtu_service.h"

#ifdef AT_COMMAND
#include "at_product.h"
#endif
#include "dtu_board.h"
#include "dtu_config.h"
#include "dtu_log.h"
#include "dtu_run.h"
#include "dtu_service_internal.h"
#include "dtu_storage.h"
#include "dtu_transport.h"
#include "osal_debug.h"

#define DTU_SERVICE_MAX_FRAME_SIZE (DTU_CFG_MAX_FRAME_BODY + 8)

/* trace 默认关闭，打开后只累计指标，避免高频 RX 路径刷日志。 */
#if (DTU_CFG_LOG_TRACE_ENABLE != 0)
static uint32_t g_dtu_rx_batch_max = 0;
static uint32_t g_dtu_rx_batch_last = 0;
static uint32_t g_dtu_rx_total_bytes = 0;
static uint32_t g_dtu_rx_callback_count = 0;
static uint32_t g_dtu_rx_ring_high_watermark = 0;
static uint32_t g_dtu_rx_ring_overflow_count = 0;
static uint32_t g_dtu_rx_task_wakeup_count = 0;
#endif

static void dtu_service_route_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len);

static bool dtu_service_try_forward_uart0_at(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len < 2) {
        return false;
    }
    if (!((data[0] == 'A' || data[0] == 'a') && (data[1] == 'T' || data[1] == 't'))) {
        return false;
    }

#ifdef AT_COMMAND
    (void)uapi_at_channel_data_recv(AT_UART_PORT, (uint8_t *)data, len); // UART0 AT 命令优先交给系统 AT 通道。
    return true;
#else
    return false;
#endif
}

/* manager 只依赖统一 transport 接口，不展开 UART/BLE/SLE 内部细节。 */
static const dtu_transport_if_t *g_dtu_transport_table[DTU_TRANSPORT_MAX] = {
    [DTU_TRANSPORT_UART] = &g_dtu_uart_transport,
    [DTU_TRANSPORT_BLE] = &g_dtu_ble_transport,
    [DTU_TRANSPORT_SLE] = &g_dtu_sle_transport,
};

const dtu_transport_if_t *dtu_service_transport_if(dtu_transport_id_t transport_id)
{
    if (transport_id >= DTU_TRANSPORT_MAX) {
        return NULL;
    }
    return g_dtu_transport_table[transport_id];
}

void dtu_service_trace_rx_batch(uint16_t length, uint16_t accepted, uint16_t ring_used)
{
#if (DTU_CFG_LOG_TRACE_ENABLE == 0)
    unused(length);
    unused(accepted);
    unused(ring_used);
#else
    g_dtu_rx_callback_count++;
    g_dtu_rx_total_bytes += accepted;
    g_dtu_rx_batch_last = accepted;
    if (accepted > g_dtu_rx_batch_max) {
        g_dtu_rx_batch_max = accepted;
    }
    if (ring_used > g_dtu_rx_ring_high_watermark) {
        g_dtu_rx_ring_high_watermark = ring_used;
    }
    if (accepted < length) {
        g_dtu_rx_ring_overflow_count += (uint32_t)(length - accepted);
    }
#endif
}

void dtu_service_trace_rx_task_wakeup(void)
{
#if (DTU_CFG_LOG_TRACE_ENABLE == 0)
#else
    g_dtu_rx_task_wakeup_count++;
#endif
}

static void dtu_service_route_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len)
{
    if (dtu_storage_current_mode() == DTU_MODE_RUN) {
        if (transport_id == DTU_TRANSPORT_UART) {
            dtu_run_on_uart0(data, len); // RUN 模式下 UART0 作为 PC 调试/观察输入。
        } else if (transport_id == DTU_TRANSPORT_SLE) {
            dtu_run_on_sle(data, len); // RUN 模式下 SLE 下行业务数据进入透明桥接。
        }
        return;
    }

    dtu_config_on_bytes(transport_id, data, len); // CONFIG 模式下统一进入 AA55 配置协议解析。
}

void dtu_service_on_bytes(dtu_transport_id_t transport_id, const uint8_t *data, uint16_t len)
{
    if (transport_id >= DTU_TRANSPORT_MAX || data == NULL || len == 0) {
        return;
    }

    if (transport_id == DTU_TRANSPORT_UART || transport_id == DTU_TRANSPORT_SLE || transport_id == DTU_TRANSPORT_BLE) {
        dtu_board_mark_data_activity(); // 任一外部通道有数据时刷新活动灯。
    }

    if (transport_id == DTU_TRANSPORT_UART && dtu_service_try_forward_uart0_at(data, len)) {
        return;
    }

    dtu_service_route_bytes(transport_id, data, len); // 按当前模式把原始字节路由到 CONFIG 或 RUN。
}

void dtu_service_on_uart485_bytes(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) {
        return;
    }
    if (dtu_storage_current_mode() != DTU_MODE_RUN) {
        return;
    }

    dtu_board_mark_data_activity(); // 485 返回数据也计入链路活动。
    dtu_run_on_485(data, len); // RUN 模式下 485 回包进入上行透明桥接。
}

void dtu_service_reply(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, const uint8_t *body, uint16_t body_len)
{
    const dtu_transport_if_t *transport_if = dtu_service_transport_if(transport_id);
    uint8_t frame[DTU_SERVICE_MAX_FRAME_SIZE];
    uint16_t frame_len = 0;
    errcode_t ret;

    if (transport_if == NULL || transport_if->send == NULL) {
        dtu_log_error("tx transport unsupported: %s", dtu_log_transport_name(transport_id));
        return;
    }

    ret = dtu_config_pack_response(cmd, seq, body, body_len, frame, sizeof(frame), &frame_len); // 先封装 AA55 完整响应帧。
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("tx pack failed: cmd=%s ret=0x%X", dtu_log_cmd_name(cmd), ret);
        return;
    }

    ret = transport_if->send(frame, frame_len); // 响应沿原 transport 回到发起端。
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("tx send failed: transport=%s cmd=%s ret=0x%X",
            dtu_log_transport_name(transport_id), dtu_log_cmd_name(cmd), ret);
    }
}

void dtu_service_reply_status(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, uint8_t status)
{
    dtu_service_reply(transport_id, cmd, seq, &status, 1);
}

static errcode_t dtu_service_init_transport(dtu_transport_id_t transport_id)
{
    const dtu_transport_if_t *transport_if = dtu_service_transport_if(transport_id);
    errcode_t ret;

    if (transport_if == NULL || transport_if->init == NULL) {
        return ERRCODE_SUCC;
    }

    ret = transport_if->init(); // 通过统一接口初始化具体 UART/BLE/SLE 通道。
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("transport init failed: %s ret=0x%x", dtu_log_transport_name(transport_id), ret);
    }
    return ret;
}

errcode_t dtu_service_init(void)
{
    errcode_t load_ret;
    errcode_t ret;

    dtu_log_info("manager init begin");
    load_ret = dtu_storage_load(); // 加载 NV 配置并采样拨码模式。
    dtu_log_info("manager storage load done: ret=0x%x mode=%u",
        load_ret, (uint32_t)dtu_storage_current_mode());

    ret = dtu_board_init(); // 初始化 DIP、状态灯和活动灯。
    dtu_log_info("manager board init end: ret=0x%X", ret);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    dtu_log_info("manager uart init begin");
    ret = dtu_service_init_transport(DTU_TRANSPORT_UART); // UART0 始终启动，作为配置口或 PC 观察口。
    dtu_log_info("manager uart init end: ret=0x%X", ret);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    if (dtu_storage_current_mode() == DTU_MODE_RUN) {
        dtu_log_info("manager sle init begin");
        ret = dtu_service_init_transport(DTU_TRANSPORT_SLE); // RUN 模式启动 SLE 业务通道。
        dtu_log_info("manager sle init end: ret=0x%X", ret);
    } else {
        dtu_log_info("manager ble init begin");
        ret = dtu_service_init_transport(DTU_TRANSPORT_BLE); // CONFIG 模式启动 BLE 配置通道。
        dtu_log_info("manager ble init end: ret=0x%X", ret);
    }
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    dtu_log_boot(load_ret); // 所有 transport 就绪后打印真实生效配置快照。
    dtu_log_info("manager init end");
    return ERRCODE_SUCC;
}
