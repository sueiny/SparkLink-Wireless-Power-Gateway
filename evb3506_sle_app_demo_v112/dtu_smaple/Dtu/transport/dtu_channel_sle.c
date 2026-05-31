#include "dtu_transport.h"

#include "dtu_log.h"
#include "dtu_service.h"
#include "dtu_storage.h"
#include "hal_reboot.h"
#include "osal_addr.h"
#include "osal_debug.h"
#include "securec.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "soc_osal.h"

/* SLE transport 职责：
 * 1. RUN 模式下提供星闪 server 通道
 * 2. SLE write 回调只负责入队，不做 Modbus 过滤和业务处理
 * 3. 任务线程批量取出字节，统一喂给 dtu_service_on_bytes(SLE, ...)
 * 4. send() 只通过 SLE notify/indicate 回传完整数据
 */

#define dtu_sle_log(fmt, ...)              dtu_log_transport("SLE", fmt, ##__VA_ARGS__)
#define DTU_SLE_UUID_LEN_2                 2
#define DTU_SLE_ADV_HANDLE                 1
/* SLE 广播发射功率。
 * 0x7F 是 SDK 文档定义的“不设置特定发送功率”，避免进入固定功率 RF 校准路径。
 */
#define DTU_SLE_ADV_TX_POWER               0x7F
#define DTU_SLE_CONN_INTERVAL              0x14
#define DTU_SLE_CONN_LATENCY               0x1F3
#define DTU_SLE_CONN_TIMEOUT               0x1F4
#define DTU_SLE_MTU_SIZE                   300
#define DTU_SLE_SERVICE_UUID               0xFDF0
#define DTU_SLE_PROPERTY_UUID              0xFDF1
#define DTU_SLE_SERVER_APP_UUID            0x4454
#define DTU_SLE_ADV_DISCOVERY_LEN          1
#define DTU_SLE_ADV_UUID_LEN               2
#define DTU_SLE_ADV_NAME_MAX_LEN           15
#define DTU_SLE_ADV_TYPE_DISCOVERY_LEVEL   0x01
#define DTU_SLE_ADV_TYPE_UUID16_COMPLETE   0x05
#define DTU_SLE_ADV_TYPE_LOCAL_NAME        0x0B
#define DTU_SLE_NOTIFY_ENABLE              0x0001

typedef struct {
    uint8_t server_id;
    uint16_t service_handle;
    uint16_t property_handle;
    uint16_t conn_handle;
    bool connected;
    bool callbacks_registered;
    bool service_started;
    uint8_t rx_ring[DTU_CFG_RING_BUFFER_SIZE];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    osal_semaphore rx_sem;
    bool rx_sem_ready;
    bool task_started;
} dtu_sle_transport_ctx_t;

static dtu_sle_transport_ctx_t g_dtu_sle_ctx;

static uint8_t g_dtu_sle_uuid_base[SLE_UUID_LEN] = {
    0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
    0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* 返回 SLE transport 私有上下文。 */
static dtu_sle_transport_ctx_t *dtu_sle_ctx(void)
{
    return &g_dtu_sle_ctx;
}

/* 按 SLE 示例格式生成 16bit UUID。 */
static void dtu_sle_uuid_set_u16(uint16_t uuid16, sle_uuid_t *uuid)
{
    if (uuid == NULL) {
        return;
    }

    (void)memcpy_s(uuid->uuid, sizeof(uuid->uuid), g_dtu_sle_uuid_base, sizeof(g_dtu_sle_uuid_base));
    uuid->len = DTU_SLE_UUID_LEN_2;
    uuid->uuid[14] = (uint8_t)(uuid16 & 0xFF);
    uuid->uuid[15] = (uint8_t)((uuid16 >> 8) & 0xFF);
}

/* 计算 SLE ring buffer 已使用字节数。 */
static uint16_t dtu_sle_ring_used(const dtu_sle_transport_ctx_t *ctx)
{
    if (ctx->rx_head >= ctx->rx_tail) {
        return (uint16_t)(ctx->rx_head - ctx->rx_tail);
    }
    return (uint16_t)(DTU_CFG_RING_BUFFER_SIZE - ctx->rx_tail + ctx->rx_head);
}

/* 向 SLE ring buffer 推入单字节。 */
static bool dtu_sle_ring_push(uint8_t byte)
{
    dtu_sle_transport_ctx_t *ctx = dtu_sle_ctx();
    uint16_t next = (uint16_t)((ctx->rx_head + 1) % DTU_CFG_RING_BUFFER_SIZE);

    if (next == ctx->rx_tail) {
        return false;
    }
    ctx->rx_ring[ctx->rx_head] = byte;
    ctx->rx_head = next;
    return true;
}

/* 从 SLE ring buffer 弹出单字节。 */
static bool dtu_sle_ring_pop(uint8_t *byte)
{
    dtu_sle_transport_ctx_t *ctx = dtu_sle_ctx();

    if (ctx->rx_tail == ctx->rx_head) {
        return false;
    }
    *byte = ctx->rx_ring[ctx->rx_tail];
    ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1) % DTU_CFG_RING_BUFFER_SIZE);
    return true;
}



/* 获取 SLE 广播名，复用 DTU 统一设备名。 */
static uint8_t dtu_sle_get_device_name(uint8_t *name_buf, uint8_t buf_len)
{
    uint8_t tmp[DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t name_len;

    if (name_buf == NULL || buf_len == 0) {
        return 0;
    }

    name_len = dtu_storage_get_device_name(tmp, sizeof(tmp));
    if (name_len > buf_len) {
        name_len = buf_len;
    }
    if (name_len > 0) {
        (void)memcpy_s(name_buf, buf_len, tmp, name_len);
    }
    return name_len;
}

/* 设置 SLE 本地地址，当前复用 Kconfig/storage 中的 DTU MAC。 */
static void dtu_sle_set_local_addr(void)
{
    sle_addr_t addr = {0};

    addr.type = 0;
    dtu_storage_get_device_mac(addr.addr);
    (void)sle_set_local_addr(&addr);
}

/* 配置 SLE 广播参数。 */
static errcode_t dtu_sle_set_announce_param(void)
{
    sle_announce_param_t param = {0};

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = DTU_SLE_ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = 0x07;
    param.announce_interval_min = 0xC8;
    param.announce_interval_max = 0xC8;
    param.conn_interval_min = DTU_SLE_CONN_INTERVAL;
    param.conn_interval_max = DTU_SLE_CONN_INTERVAL;
    param.conn_max_latency = 0x1F3;
    param.conn_supervision_timeout = DTU_SLE_CONN_TIMEOUT;
    param.announce_tx_power = DTU_SLE_ADV_TX_POWER;
    param.own_addr.type = 0;
    dtu_storage_get_device_mac(param.own_addr.addr);
    return sle_set_announce_param(DTU_SLE_ADV_HANDLE, &param);
}

/* 配置 SLE 广播数据和扫描响应。 */
static errcode_t dtu_sle_set_announce_data(void)
{
    uint8_t adv_data[8] = {0};
    uint8_t scan_rsp[DTU_SLE_ADV_NAME_MAX_LEN + 4] = {0};
    uint8_t name[DTU_SLE_ADV_NAME_MAX_LEN] = {0};
    uint8_t adv_len = 0;
    uint8_t rsp_len = 0;
    uint8_t name_len;
    sle_announce_data_t data = {0};
    errcode_t ret;

    adv_data[adv_len++] = DTU_SLE_ADV_TYPE_DISCOVERY_LEVEL;
    adv_data[adv_len++] = DTU_SLE_ADV_DISCOVERY_LEN;
    adv_data[adv_len++] = SLE_ANNOUNCE_LEVEL_NORMAL;
    adv_data[adv_len++] = DTU_SLE_ADV_TYPE_UUID16_COMPLETE;
    adv_data[adv_len++] = DTU_SLE_ADV_UUID_LEN;
    adv_data[adv_len++] = (uint8_t)(DTU_SLE_SERVICE_UUID & 0xFF);
    adv_data[adv_len++] = (uint8_t)((DTU_SLE_SERVICE_UUID >> 8) & 0xFF);

    name_len = dtu_sle_get_device_name(name, sizeof(name));
    if (name_len > 0) {
        scan_rsp[rsp_len++] = DTU_SLE_ADV_TYPE_LOCAL_NAME;
        scan_rsp[rsp_len++] = name_len;
        (void)memcpy_s(&scan_rsp[rsp_len], sizeof(scan_rsp) - rsp_len, name, name_len);
        rsp_len = (uint8_t)(rsp_len + name_len);
    }

    data.announce_data = adv_data;
    data.announce_data_len = adv_len;
    data.seek_rsp_data = scan_rsp;
    data.seek_rsp_data_len = rsp_len;

    ret = sle_set_announce_data(DTU_SLE_ADV_HANDLE, &data);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("set announce data failed: 0x%x", ret);
    }
    return ret;
}

/* 启动 SLE 广播。 */
static errcode_t dtu_sle_start_announce(void)
{
    errcode_t ret;

    ret = dtu_sle_set_announce_param();
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("set announce param failed: 0x%x", ret);
        return ret;
    }
    ret = dtu_sle_set_announce_data();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = sle_start_announce(DTU_SLE_ADV_HANDLE);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("start announce failed: 0x%x", ret);
    }
    return ret;
}

/* 添加 DTU SLE service。 */
static errcode_t dtu_sle_add_service(void)
{
    sle_uuid_t service_uuid = {0};
    errcode_t ret;

    dtu_sle_uuid_set_u16(DTU_SLE_SERVICE_UUID, &service_uuid);
    ret = ssaps_add_service_sync(dtu_sle_ctx()->server_id, &service_uuid, 1, &dtu_sle_ctx()->service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("add service failed: 0x%x", ret);
    }
    return ret;
}

/* 添加 DTU SLE 收发 property。 */
static errcode_t dtu_sle_add_property(void)
{
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};
    uint8_t init_value[2] = {0x12, 0x34};
    uint8_t desc_value[2] = {
        (uint8_t)(DTU_SLE_NOTIFY_ENABLE & 0xFF),
        (uint8_t)((DTU_SLE_NOTIFY_ENABLE >> 8) & 0xFF)
    };
    errcode_t ret;

    dtu_sle_uuid_set_u16(DTU_SLE_PROPERTY_UUID, &property.uuid);
    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
        SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP |
        SSAP_OPERATE_INDICATION_BIT_WRITE |
        SSAP_OPERATE_INDICATION_BIT_NOTIFY;
    property.value_len = sizeof(init_value);
    property.value = init_value;

    ret = ssaps_add_property_sync(dtu_sle_ctx()->server_id, dtu_sle_ctx()->service_handle,
        &property, &dtu_sle_ctx()->property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("add property failed: 0x%x", ret);
        return ret;
    }

    descriptor.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    descriptor.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    descriptor.value = desc_value;
    descriptor.value_len = sizeof(desc_value);

    ret = ssaps_add_descriptor_sync(dtu_sle_ctx()->server_id, dtu_sle_ctx()->service_handle,
        dtu_sle_ctx()->property_handle, &descriptor);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("add descriptor failed: 0x%x", ret);
    }
    return ret;
}

/* 注册并启动 DTU SLE server。 */
static errcode_t dtu_sle_register_server(void)
{
    sle_uuid_t app_uuid = {0};
    ssap_exchange_info_t info = {0};
    errcode_t ret;

    dtu_sle_uuid_set_u16(DTU_SLE_SERVER_APP_UUID, &app_uuid);
    ret = ssaps_register_server(&app_uuid, &dtu_sle_ctx()->server_id);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("register server failed: 0x%x", ret);
        return ret;
    }
    ret = dtu_sle_add_service();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = dtu_sle_add_property();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    info.mtu_size = DTU_SLE_MTU_SIZE;
    info.version = 1;
    (void)ssaps_set_info(dtu_sle_ctx()->server_id, &info);

    ret = ssaps_start_service(dtu_sle_ctx()->server_id, dtu_sle_ctx()->service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("start service failed: 0x%x", ret);
    }
    return ret;
}

/* SLE enable 完成后再建 server/service。 */
static void dtu_sle_enable_cb(errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("enable failed: 0x%x", status);
        return;
    }

    dtu_sle_set_local_addr();
    if (dtu_sle_register_server() != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("register server callback failed");
    }
}

/* SLE service 启动回调：启动广播。 */
static void dtu_sle_start_service_cb(uint8_t server_id, uint16_t handle, errcode_t status)
{
    unused(server_id);
    unused(handle);

    if (status != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("service start failed: 0x%x", status);
        return;
    }

    dtu_sle_ctx()->service_started = true;
    (void)dtu_sle_start_announce();
}

/* SLE 写回调：只入队，不在 SLE service 线程里做解析。 */
static void dtu_sle_write_request_cb(uint8_t server_id, uint16_t conn_id,
    ssaps_req_write_cb_t *write_cb_para, errcode_t status)
{
    uint16_t accepted = 0;

    unused(server_id);
    if (status != ERRCODE_SLE_SUCCESS || write_cb_para == NULL) {
        return;
    }
    if (write_cb_para->handle != dtu_sle_ctx()->property_handle ||
        write_cb_para->value == NULL || write_cb_para->length == 0) {
        return;
    }

    dtu_sle_ctx()->conn_handle = conn_id;
    dtu_sle_ctx()->connected = true;
    for (uint16_t i = 0; i < write_cb_para->length; i++) {
        if (!dtu_sle_ring_push(write_cb_para->value[i])) {
            break;
        }
        accepted++;
        
    }
    dtu_service_trace_rx_batch(write_cb_para->length, accepted, dtu_sle_ring_used(dtu_sle_ctx()));
    osal_sem_up(&dtu_sle_ctx()->rx_sem);
}

/* SLE 连接状态回调：保存连接句柄，断开后重启广播。 */
static void dtu_sle_connect_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);
    unused(disc_reason);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        sle_connection_param_update_t param = {0};

        dtu_sle_ctx()->conn_handle = conn_id;
        dtu_sle_ctx()->connected = true;
        param.conn_id = conn_id;
        param.interval_min = DTU_SLE_CONN_INTERVAL;
        param.interval_max = DTU_SLE_CONN_INTERVAL;
        param.max_latency = DTU_SLE_CONN_LATENCY;
        param.supervision_timeout = DTU_SLE_CONN_TIMEOUT;
        (void)sle_update_connect_param(&param);
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        dtu_sle_ctx()->conn_handle = 0;
        dtu_sle_ctx()->connected = false;
        (void)dtu_sle_start_announce();
    }
}

/* 注册 SLE announce / connection / SSAPS 回调。 */
static errcode_t dtu_sle_register_callbacks(void)
{
    sle_announce_seek_callbacks_t announce_cb = {0};
    sle_connection_callbacks_t conn_cb = {0};
    ssaps_callbacks_t ssaps_cb = {0};
    errcode_t ret;

    if (dtu_sle_ctx()->callbacks_registered) {
        return ERRCODE_SUCC;
    }

    announce_cb.sle_enable_cb = dtu_sle_enable_cb;
    ret = sle_announce_seek_register_callbacks(&announce_cb);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("register announce cb failed: 0x%x", ret);
        return ret;
    }

    conn_cb.connect_state_changed_cb = dtu_sle_connect_state_changed_cb;
    ret = sle_connection_register_callbacks(&conn_cb);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("register conn cb failed: 0x%x", ret);
        return ret;
    }

    ssaps_cb.start_service_cb = dtu_sle_start_service_cb;
    ssaps_cb.write_request_cb = dtu_sle_write_request_cb;
    ret = ssaps_register_callbacks(&ssaps_cb);
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("register ssaps cb failed: 0x%x", ret);
        return ret;
    }

    dtu_sle_ctx()->callbacks_registered = true;
    return ERRCODE_SUCC;
}

/* SLE 接收任务：把回调入队的数据批量提交给 service。 */
static void *dtu_sle_task(const char *arg)
{
    uint8_t batch[DTU_CFG_TRANSPORT_RX_BATCH_SIZE];

    unused(arg);
    while (1) {
        uint16_t count = 0;

        while (count < sizeof(batch) && dtu_sle_ring_pop(&batch[count])) {
            count++;
        }
        if (count > 0) {
            dtu_service_on_bytes(DTU_TRANSPORT_SLE, batch, count);
            continue;
        }

        if (dtu_storage_is_reboot_pending()) {
            osal_msleep(DTU_CFG_REBOOT_DELAY_MS);
            hal_reboot_chip();
        }

        dtu_service_trace_rx_task_wakeup();
        if (osal_sem_down(&dtu_sle_ctx()->rx_sem) != OSAL_SUCCESS) {
            osal_msleep(DTU_CFG_TASK_IDLE_RETRY_MS);
        }
    }

    return NULL;
}

/* 初始化 SLE transport。 */
static errcode_t dtu_sle_transport_init_impl(void)
{
    osal_task *task;
    errcode_t ret;

    if (!dtu_sle_ctx()->rx_sem_ready) {
        ret = osal_sem_binary_sem_init(&dtu_sle_ctx()->rx_sem, 0);
        if (ret != OSAL_SUCCESS) {
            dtu_sle_log("sem init failed: 0x%x", ret);
            return ERRCODE_FAIL;
        }
        dtu_sle_ctx()->rx_sem_ready = true;
    }

    if (!dtu_sle_ctx()->task_started) {
        task = osal_kthread_create((osal_kthread_handler)dtu_sle_task, NULL,
            DTU_CFG_SLE_TASK_NAME, DTU_CFG_TRANSPORT_TASK_STACK_SIZE);
        if (task == NULL) {
            dtu_sle_log("task create failed");
            return ERRCODE_FAIL;
        }
        osal_kthread_set_priority(task, DTU_CFG_TRANSPORT_TASK_PRIO);
        dtu_sle_ctx()->task_started = true;
    }

    ret = dtu_sle_register_callbacks();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    ret = enable_sle();
    if (ret != ERRCODE_SLE_SUCCESS) {
        dtu_sle_log("enable request failed: 0x%x", ret);
        return ret;
    }
    return ERRCODE_SUCC;
}

/* 通过 SLE notify/indicate 发送完整数据。 */
static errcode_t dtu_sle_transport_send_impl(const uint8_t *data, uint16_t len)
{
    ssaps_ntf_ind_t param = {0};
    uint8_t *value;
    errcode_t ret;

    if (data == NULL || len == 0) {
        return ERRCODE_FAIL;
    }
    if (!dtu_sle_ctx()->service_started || !dtu_sle_ctx()->connected || dtu_sle_ctx()->property_handle == 0) {
        dtu_sle_log("there is no active connection to send data");
        return ERRCODE_FAIL;
    }

    value = osal_vmalloc(len);
    if (value == NULL) {
        return ERRCODE_MALLOC;
    }
    if (memcpy_s(value, len, data, len) != EOK) {
        osal_vfree(value);
        return ERRCODE_FAIL;
    }

    param.handle = dtu_sle_ctx()->property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = value;
    param.value_len = len;
    ret = ssaps_notify_indicate(dtu_sle_ctx()->server_id, dtu_sle_ctx()->conn_handle, &param);
    osal_vfree(value);
    return (ret == ERRCODE_SLE_SUCCESS) ? ERRCODE_SUCC : ret;
}

const dtu_transport_if_t g_dtu_sle_transport = {
    .name = "SLE",
    .init = dtu_sle_transport_init_impl,
    .send = dtu_sle_transport_send_impl
};
