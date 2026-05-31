#include "dtu_transport.h"

#include <string.h>

#include "bts_def.h"
#include "bts_le_gap.h"
#include "bts_gatt_stru.h"
#include "bts_gatt_server.h"
#include "dtu_log.h"
#include "dtu_service.h"
#include "dtu_storage.h"
#include "hal_reboot.h"
#include "osal_addr.h"
#include "osal_debug.h"
#include "securec.h"
#include "soc_osal.h"

/* BLE transport 职责：
 * 1. 提供与 UART 并行的第二条 DTU 配置接入通道。
 * 2. GATT write 收到原始字节后，直接喂给 dtu_service_on_bytes(BLE, ...)。
 * 3. 回复发送统一走 notify，业务层不感知 BLE 细节。
 */

#define DTU_BLE_UUID_LEN_2                2
#define DTU_BLE_CCC_UUID                  0x2902
#define DTU_BLE_ADV_DATA_MAX_LEN          31
#define DTU_BLE_SCAN_RSP_MAX_LEN          31
#define DTU_BLE_DEVICE_NAME_MAX_LEN       15
#define DTU_BLE_LOCAL_NAME_TYPE           0x09
#define DTU_BLE_FLAGS_TYPE                0x01
#define DTU_BLE_FLAGS_LEN                 2
#define DTU_BLE_FLAGS_VALUE               0x06
#define DTU_BLE_ADV_HANDLE                0x01
#define DTU_BLE_ADV_MIN_INTERVAL          0x20
#define DTU_BLE_ADV_MAX_INTERVAL          0x60
#define DTU_BLE_ADV_FOREVER_DURATION      0
#define DTU_BLE_ADV_CHANNEL_MAP_DEFAULT   0x07
#define DTU_BLE_SERVER_APP_UUID           0x4454
#define DTU_BLE_SERVICE_UUID              0xFDF0
#define DTU_BLE_CHAR_UUID                 0xFDF1
#define dtu_ble_log(fmt, ...)             dtu_log_transport("BLE", fmt, ##__VA_ARGS__)

typedef struct {
    uint8_t server_id;
    uint16_t service_handle;
    uint16_t value_handle;
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
} dtu_ble_transport_ctx_t;

static dtu_ble_transport_ctx_t g_dtu_ble_ctx;

/* 返回 BLE transport 私有上下文。
 * 当前 BLE 通道的运行时状态都集中保存在这里：
 * 1. server / service / characteristic handle
 * 2. 当前连接句柄
 * 3. 回调是否已注册、service 是否已启动
 * 这样可以避免 BLE 相关状态散落在多个静态变量里。
 */
static dtu_ble_transport_ctx_t *dtu_ble_ctx(void)
{
    return &g_dtu_ble_ctx;
}

/* 计算 BLE ring buffer 已使用字节数。
 * BLE 写回调不直接做协议解析，只负责把原始字节尽快塞进本地队列。
 */
static uint16_t dtu_ble_ring_used(const dtu_ble_transport_ctx_t *ctx)
{
    if (ctx->rx_head >= ctx->rx_tail) {
        return (uint16_t)(ctx->rx_head - ctx->rx_tail);
    }
    return (uint16_t)(DTU_CFG_RING_BUFFER_SIZE - ctx->rx_tail + ctx->rx_head);
}

/* 向 BLE ring buffer 推入单字节。 */
static bool dtu_ble_ring_push(uint8_t byte)
{
    dtu_ble_transport_ctx_t *ctx = dtu_ble_ctx();
    uint16_t next = (uint16_t)((ctx->rx_head + 1) % DTU_CFG_RING_BUFFER_SIZE);

    if (next == ctx->rx_tail) {
        return false;
    }
    ctx->rx_ring[ctx->rx_head] = byte;
    ctx->rx_head = next;
    return true;
}

/* 从 BLE ring buffer 弹出单字节。 */
static bool dtu_ble_ring_pop(uint8_t *byte)
{
    dtu_ble_transport_ctx_t *ctx = dtu_ble_ctx();

    if (ctx->rx_tail == ctx->rx_head) {
        return false;
    }
    *byte = ctx->rx_ring[ctx->rx_tail];
    ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1) % DTU_CFG_RING_BUFFER_SIZE);
    return true;
}



/* 将 16bit UUID 填充为 bt_uuid_t。
 * BT 示例里大量使用 16bit UUID，本地封装一个小工具后，
 * 后面新增 service / characteristic / descriptor 时都能复用。
 */
static void dtu_ble_u16_to_uuid(uint16_t uuid16, bt_uuid_t *uuid)
{
    uint8_t data[DTU_BLE_UUID_LEN_2] = {
        (uint8_t)(uuid16 >> 8),
        (uint8_t)(uuid16 & 0xFF)
    };

    uuid->uuid_len = DTU_BLE_UUID_LEN_2;
    (void)memcpy_s(uuid->uuid, uuid->uuid_len, data, sizeof(data));
}

/* 生成广播名，优先复用 DTU 设备名。
 * 这里直接复用 storage 层已经统一维护的设备名生成逻辑，
 * 这样 UART / BLE 两条配置通道看到的设备身份是一致的。
 * 如果设备名暂时不可用，则回退到固定名字 DTU_CFG。
 */
static uint8_t dtu_ble_get_device_name(uint8_t *name_buf, uint8_t buf_len)
{
    uint8_t tmp[DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t name_len;

    if (name_buf == NULL || buf_len == 0) {
        return 0;
    }

    name_len = dtu_storage_get_device_name(tmp, sizeof(tmp));
    if (name_len == 0) {
        static const uint8_t fallback_name[] = "DTU_CFG";
        uint8_t copy_len = (uint8_t)((sizeof(fallback_name) - 1 < buf_len) ? (sizeof(fallback_name) - 1) : buf_len);

        (void)memcpy_s(name_buf, buf_len, fallback_name, copy_len);
        return copy_len;
    }

    if (name_len > buf_len) {
        name_len = buf_len;
    }
    (void)memcpy_s(name_buf, buf_len, tmp, name_len);
    return name_len;
}

/* 配置 BLE 广播数据和扫描响应。
 * 当前策略保持尽量简单：
 * 1. adv data 只放 flags
 * 2. scan rsp 放本地设备名
 * 这样手机和 PC 侧更容易直接发现设备。
 */
static errcode_t dtu_ble_set_adv_data(void)
{
    uint8_t adv_data[DTU_BLE_ADV_DATA_MAX_LEN] = {0};
    uint8_t scan_rsp[DTU_BLE_SCAN_RSP_MAX_LEN] = {0};
    uint8_t device_name[DTU_BLE_DEVICE_NAME_MAX_LEN] = {0};
    uint8_t name_len;
    gap_ble_config_adv_data_t cfg_adv_data = {0};
    uint8_t adv_len = 0;
    uint8_t rsp_len = 0;
    errcode_t ret;

    adv_data[adv_len++] = DTU_BLE_FLAGS_LEN;
    adv_data[adv_len++] = DTU_BLE_FLAGS_TYPE;
    adv_data[adv_len++] = DTU_BLE_FLAGS_VALUE;

    name_len = dtu_ble_get_device_name(device_name, sizeof(device_name));
    if (name_len > 0) {
        scan_rsp[rsp_len++] = (uint8_t)(name_len + 1);
        scan_rsp[rsp_len++] = DTU_BLE_LOCAL_NAME_TYPE;
        (void)memcpy_s(&scan_rsp[rsp_len], sizeof(scan_rsp) - rsp_len, device_name, name_len);
        rsp_len = (uint8_t)(rsp_len + name_len);
    }

    cfg_adv_data.adv_data = adv_data;
    cfg_adv_data.adv_length = adv_len;
    cfg_adv_data.scan_rsp_data = scan_rsp;
    cfg_adv_data.scan_rsp_length = rsp_len;

    ret = gap_ble_set_adv_data(DTU_BLE_ADV_HANDLE, &cfg_adv_data);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("set adv data failed: 0x%x", ret);
    }
    return ret;
}

/* 启动 DTU BLE 广播。
 * BLE service 启动成功后，会在 start_service 回调里调用这个函数。
 * 这样可以保证：
 * 1. 对端在扫描到设备时，GATT service 已经准备好
 * 2. 不会出现先广播、后建表导致连接后服务未就绪的问题
 */
static errcode_t dtu_ble_start_adv(void)
{
    gap_ble_adv_params_t adv_param = {0};
    errcode_t ret;

    ret = dtu_ble_set_adv_data();
    if (ret != ERRCODE_BT_SUCCESS) {
        return ret;
    }

    adv_param.min_interval = DTU_BLE_ADV_MIN_INTERVAL;
    adv_param.max_interval = DTU_BLE_ADV_MAX_INTERVAL;
    adv_param.duration = DTU_BLE_ADV_FOREVER_DURATION;
    adv_param.peer_addr.type = BT_ADDRESS_TYPE_PUBLIC_DEVICE_ADDRESS;
    adv_param.channel_map = DTU_BLE_ADV_CHANNEL_MAP_DEFAULT;
    adv_param.adv_type = GAP_BLE_ADV_CONN_SCAN_UNDIR;
    adv_param.adv_filter_policy = GAP_BLE_ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    (void)memset_s(&adv_param.peer_addr.addr, BD_ADDR_LEN, 0, BD_ADDR_LEN);

    ret = gap_ble_set_adv_param(DTU_BLE_ADV_HANDLE, &adv_param);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("set adv param failed: 0x%x", ret);
        return ret;
    }

    ret = gap_ble_start_adv(DTU_BLE_ADV_HANDLE);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("start adv failed: 0x%x", ret);
    }
    return ret;
}

/* 为 DTU service 添加单个收发特征。
 * 当前 BLE 通道只保留“一条特征”：
 * 1. 写入方向：对端把 DTU 原始协议帧写进来
 * 2. 通知方向：设备把 DTU 原始响应帧 notify 回去
 *
 * 这样 BLE 通道与 UART 的职责完全一致：
 * 只负责字节收发，不引入第二套协议。
 */
static errcode_t dtu_ble_add_characteristic(uint8_t server_id, uint16_t service_handle)
{
    gatts_add_chara_info_t character = {0};
    gatts_add_character_result_t result = {0};
    gatts_add_desc_info_t ccc = {0};
    uint16_t ccc_handle = 0;
    uint8_t init_value[2] = {0x12, 0x34};
    uint8_t ccc_value[2] = {0x01, 0x00};
    bt_uuid_t ccc_uuid = {0};
    errcode_t ret;

    dtu_ble_u16_to_uuid(DTU_BLE_CHAR_UUID, &character.chara_uuid);
    character.properties = GATT_CHARACTER_PROPERTY_BIT_NOTIFY | GATT_CHARACTER_PROPERTY_BIT_WRITE_NO_RSP;
    character.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    character.value_len = sizeof(init_value);
    character.value = init_value;

    ret = gatts_add_characteristic_sync(server_id, service_handle, &character, &result);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("add characteristic failed: 0x%x", ret);
        return ret;
    }
    dtu_ble_ctx()->value_handle = result.value_handle;

    dtu_ble_u16_to_uuid(DTU_BLE_CCC_UUID, &ccc_uuid);
    ccc.desc_uuid = ccc_uuid;
    ccc.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    ccc.value_len = sizeof(ccc_value);
    ccc.value = ccc_value;

    ret = gatts_add_descriptor_sync(server_id, service_handle, &ccc, &ccc_handle);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("add ccc failed: 0x%x", ret);
    }
    return ret;
}

/* 注册 DTU BLE GATT service。
 * 这一步只完成 GATT 结构搭建：
 * 1. add service
 * 2. add characteristic
 * 3. start service
 * 真正的业务字节流处理仍然统一交给 dtu_service_on_bytes()。
 */
static errcode_t dtu_ble_register_service(void)
{
    bt_uuid_t service_uuid = {0};
    errcode_t ret;

    dtu_ble_u16_to_uuid(DTU_BLE_SERVICE_UUID, &service_uuid);
    ret = gatts_add_service_sync(dtu_ble_ctx()->server_id, &service_uuid, true, &dtu_ble_ctx()->service_handle);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("add service failed: 0x%x", ret);
        return ret;
    }

    ret = dtu_ble_add_characteristic(dtu_ble_ctx()->server_id, dtu_ble_ctx()->service_handle);
    if (ret != ERRCODE_BT_SUCCESS) {
        return ret;
    }

    ret = gatts_start_service(dtu_ble_ctx()->server_id, dtu_ble_ctx()->service_handle);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("start service failed: 0x%x", ret);
    }
    return ret;
}

/* BLE 使能完成后，注册 DTU server 并创建 GATT service。
 * enable_ble() 是异步流程，所以真正的 server/service 注册放在
 * ble_enable 回调里完成，而不是在 init() 里直接同步做完。
 */
static void dtu_ble_enable_cb(errcode_t status)
{
    bt_uuid_t app_uuid = {0};
    uint8_t server_id = 0;
    errcode_t ret;

    if (status != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("enable failed: 0x%x", status);
        return;
    }

    dtu_ble_u16_to_uuid(DTU_BLE_SERVER_APP_UUID, &app_uuid);
    ret = gatts_register_server(&app_uuid, &server_id);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("register server failed: 0x%x", ret);
        return;
    }

    dtu_ble_ctx()->server_id = server_id;
    ret = dtu_ble_register_service();
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("register service failed: 0x%x", ret);
    }
}

/* service 启动后开始广播。
 * 只有当 GATT service 真正进入 started 状态后，才开始对外广播。
 * 这样外部客户端一旦连上来，就能立刻看到完整的 DTU BLE 服务。
 */
static void dtu_ble_service_start_cb(uint8_t server_id, uint16_t handle, errcode_t status)
{
    unused(server_id);
    unused(handle);

    if (status != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("service start cb failed: 0x%x", status);
        return;
    }

    dtu_ble_ctx()->service_started = true;
    (void)dtu_ble_start_adv();
}

/* BLE 写请求回调：把原始字节直接喂给统一 DTU service。
 * 这是 BLE 通道最关键的接入点：
 * 1. 这里只校验是不是 DTU 那条 characteristic
 * 2. 不做协议解析
 * 3. 不做配置处理
 * 4. 直接调用 dtu_service_on_bytes(DTU_TRANSPORT_BLE, ...)
 *
 * 也就是说，BLE 和 UART 只是“入口不同”，进入 service 后走的是同一套
 * protocol / manager / mode / storage 逻辑。
 */
static void dtu_ble_write_req_cb(uint8_t server_id, uint16_t conn_id,
    gatts_req_write_cb_t *write_cb_para, errcode_t status)
{
    uint16_t accepted = 0;

    unused(server_id);
    unused(conn_id);

    if (status != ERRCODE_BT_SUCCESS || write_cb_para == NULL) {
        return;
    }
    if (write_cb_para->handle != dtu_ble_ctx()->value_handle || write_cb_para->value == NULL || write_cb_para->length == 0) {
        return;
    }

    /* 某些平台上首个 BLE 连接句柄可能就是 0，不能把 0 当成“未连接”。
     * 这里收到有效写请求时，顺手用本次 conn_id 刷新当前活动连接。
     */
    dtu_ble_ctx()->conn_handle = conn_id;
    dtu_ble_ctx()->connected = true;

    for (uint16_t i = 0; i < write_cb_para->length; i++) {
        if (!dtu_ble_ring_push(write_cb_para->value[i])) {
            break;
        }
        accepted++;
    }

    dtu_service_trace_rx_batch(write_cb_para->length, accepted, dtu_ble_ring_used(dtu_ble_ctx()));
    osal_sem_up(&dtu_ble_ctx()->rx_sem);
}

/* BLE 连接状态变化时记录当前连接句柄。
 * 当前实现只保存最近一次有效连接，用于后续 notify 发送响应帧。
 * 如果后面要扩展成多连接，需要在这里把单句柄改成连接表。
 */
static void dtu_ble_conn_state_change_cb(uint16_t conn_id, bd_addr_t *addr, gap_ble_conn_state_t conn_state,
    gap_ble_pair_state_t pair_state, gap_ble_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);
    unused(disc_reason);

    if (conn_state == GAP_BLE_STATE_CONNECTED) {
        dtu_ble_ctx()->conn_handle = conn_id;
        dtu_ble_ctx()->connected = true;
    } else if (conn_state == GAP_BLE_STATE_DISCONNECTED) {
        dtu_ble_ctx()->conn_handle = 0;
        dtu_ble_ctx()->connected = false;
        dtu_ble_start_adv();
    }
}

/* 注册 BLE GAP/GATTS 回调。
 * 这里只做一次性注册，避免重复 enable / 重复 init 时多次注册回调。
 */
static errcode_t dtu_ble_register_callbacks(void)
{
    gap_ble_callbacks_t gap_cb = {0};
    gatts_callbacks_t service_cb = {0};
    errcode_t ret;

    if (dtu_ble_ctx()->callbacks_registered) {
        return ERRCODE_SUCC;
    }

    gap_cb.conn_state_change_cb = dtu_ble_conn_state_change_cb;
    gap_cb.ble_enable_cb = dtu_ble_enable_cb;
    ret = gap_ble_register_callbacks(&gap_cb);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("register gap cb failed: 0x%x", ret);
        return ret;
    }

    service_cb.start_service_cb = dtu_ble_service_start_cb;
    service_cb.write_request_cb = dtu_ble_write_req_cb;
    ret = gatts_register_callbacks(&service_cb);
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("register gatts cb failed: 0x%x", ret);
        return ret;
    }

    dtu_ble_ctx()->callbacks_registered = true;
    return ERRCODE_SUCC;
}

/* BLE 解析任务：
 * 1. 从本地 ring buffer 批量取字节
 * 2. 统一喂给 dtu_service_on_bytes(BLE, ...)
 * 3. 这样 GATT write 回调上下文只负责快速入队，不会被协议解析拖慢
 */
static void *dtu_ble_task(const char *arg)
{
    uint8_t batch[DTU_CFG_TRANSPORT_RX_BATCH_SIZE];

    unused(arg);
    while (1) {
        uint16_t count = 0;

        while (count < sizeof(batch) && dtu_ble_ring_pop(&batch[count])) {
            count++;
        }
        if (count > 0) {
            dtu_service_on_bytes(DTU_TRANSPORT_BLE, batch, count);
            continue;
        }

        if (dtu_storage_is_reboot_pending()) {
            osal_msleep(DTU_CFG_REBOOT_DELAY_MS);
            hal_reboot_chip();
        }

        dtu_service_trace_rx_task_wakeup();
        if (osal_sem_down(&dtu_ble_ctx()->rx_sem) != OSAL_SUCCESS) {
            osal_msleep(DTU_CFG_TASK_IDLE_RETRY_MS);
        }
    }

    return NULL;
}

/* 通过 BLE notify 发送完整 DTU 协议帧。
 * 注意这里发送的是“已经打包好的完整 DTU 响应帧”：
 * config/manager 先把 body 组装成协议帧，再由 BLE transport 直接 notify。
 *
 * 这样 BLE 通道不会参与任何协议拼装，只承担：
 * 1. 原始字节写入
 * 2. 原始字节通知
 *
 * 这也是当前 DTU transport 设计里“低耦合”的核心。
 */
static errcode_t dtu_ble_transport_send_impl(const uint8_t *data, uint16_t len)
{
    gatts_ntf_ind_t param = {0};
    uint8_t *value;

    if (data == NULL || len == 0) {
        return ERRCODE_FAIL;
    }
    if (!dtu_ble_ctx()->service_started || !dtu_ble_ctx()->connected || dtu_ble_ctx()->value_handle == 0) {
        dtu_ble_log("there is no active connection to send data");
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

    param.attr_handle = dtu_ble_ctx()->value_handle;
    param.value = value;
    param.value_len = len;
    (void)gatts_notify_indicate(dtu_ble_ctx()->server_id, dtu_ble_ctx()->conn_handle, &param);
    osal_vfree(value);
    return ERRCODE_SUCC;
}

/* 初始化 BLE transport。
 * transport init 只做两件事：
 * 1. 注册 BLE GAP/GATTS 回调
 * 2. 请求 enable BLE
 *
 * 后续的 server 注册、service 建立、广播启动，都放在异步回调里完成。
 */
static errcode_t dtu_ble_transport_init_impl(void)
{
    osal_task *task;
    errcode_t ret;

    if (!dtu_ble_ctx()->rx_sem_ready) {
        ret = osal_sem_binary_sem_init(&dtu_ble_ctx()->rx_sem, 0);
        if (ret != OSAL_SUCCESS) {
            dtu_ble_log("sem init failed: 0x%x", ret);
            return ERRCODE_FAIL;
        }
        dtu_ble_ctx()->rx_sem_ready = true;
    }

    if (!dtu_ble_ctx()->task_started) {
        task = osal_kthread_create((osal_kthread_handler)dtu_ble_task, NULL,
            DTU_CFG_BLE_TASK_NAME, DTU_CFG_TRANSPORT_TASK_STACK_SIZE);
        if (task == NULL) {
            dtu_ble_log("task create failed");
            return ERRCODE_FAIL;
        }
        osal_kthread_set_priority(task, DTU_CFG_TRANSPORT_TASK_PRIO);
        dtu_ble_ctx()->task_started = true;
    }

    ret = dtu_ble_register_callbacks();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    ret = enable_ble();
    if (ret != ERRCODE_BT_SUCCESS) {
        dtu_ble_log("enable request failed: 0x%x", ret);
        return ret;
    }

    return ERRCODE_SUCC;
}

const dtu_transport_if_t g_dtu_ble_transport = {
    .name = "BLE",
    .init = dtu_ble_transport_init_impl,
    .send = dtu_ble_transport_send_impl
};
