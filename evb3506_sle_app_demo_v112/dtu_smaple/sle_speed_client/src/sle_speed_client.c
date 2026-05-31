/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022. All rights reserved.
 *
 * Description: SLE private service register sample of client.
 */
#include "app_init.h"
#include "systick.h"
#include "tcxo.h"
#include "los_memory.h"
#include "securec.h"
#include "soc_osal.h"
#include "common_def.h"

#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_errcode.h"
#include "sle_ssap_client.h"

#include "sle_speed_client.h"
#include "uart.h"
#include "pinctrl.h"
#include "dma.h"
#include "hal_dma.h"

#undef THIS_FILE_ID
#define THIS_FILE_ID BTH_GLE_SAMPLE_UUID_CLIENT

#define SLE_MTU_SIZE_DEFAULT        1500
#define SLE_SEEK_INTERVAL_DEFAULT   100
#define SLE_SEEK_WINDOW_DEFAULT     100
#define UUID_16BIT_LEN 2
#define UUID_128BIT_LEN 16
#define SLE_SPEED_HUNDRED   100        /* 100  */
#define SPEED_DEFAULT_CONN_INTERVAL 0x14
#define SPEED_DEFAULT_TIMEOUT_MULTIPLIER 0x1f4
#define SPEED_DEFAULT_SCAN_INTERVAL 400
#define SPEED_DEFAULT_SCAN_WINDOW 20

static int g_recv_pkt_num = 0;
static uint64_t g_count_before_get_us;
static uint64_t g_count_after_get_us;

#ifdef CONFIG_LARGE_THROUGHPUT_CLIENT
#define RECV_PKT_CNT 1000
#else
#define RECV_PKT_CNT 1
#endif
#define RSSI_AVG_COUNT 10
static int g_rssi_sum = 0;
static int g_rssi_number = 0;

static sle_announce_seek_callbacks_t g_seek_cbk = {0};
static sle_connection_callbacks_t    g_connect_cbk = {0};
static ssapc_callbacks_t             g_ssapc_cbk = {0};
static sle_addr_t                    g_remote_addr = {0};
static uint16_t                      g_conn_id = 0;
static ssapc_find_service_result_t   g_find_service_result = {0};

static uint8_t g_app_uart_rx_buff[128] = { 0 };
static uint8_t g_app_uart_tx_buff[256] = { 0 };  /* 存储待发送的数据 */
static osal_semaphore g_app_uart_rx_sem = {0};
static uint16_t my_handle = 0;


void sle_speed_connect_param_init(void)
{
    sle_default_connect_param_t param = {0};
    param.enable_filter_policy = 0;
    param.gt_negotiate = SLE_ANNOUNCE_ROLE_G_CAN_NEGO;
    param.initiate_phys = 1;
    param.max_interval = SPEED_DEFAULT_CONN_INTERVAL;
    param.min_interval = SPEED_DEFAULT_CONN_INTERVAL;
    param.scan_interval = SPEED_DEFAULT_SCAN_INTERVAL;
    param.scan_window = SPEED_DEFAULT_SCAN_WINDOW;
    param.timeout = SPEED_DEFAULT_TIMEOUT_MULTIPLIER;
    sle_default_connection_param_set(&param);
}

void sle_start_scan(void)
{
    sle_seek_param_t param = {0};
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 0;
    param.seek_interval[0] = SLE_SEEK_INTERVAL_DEFAULT;
    param.seek_window[0] = SLE_SEEK_WINDOW_DEFAULT;
    sle_set_seek_param(&param);
    sle_start_seek();
}

void sle_sample_sle_enable_cbk(errcode_t status)
{
    if (status == 0) {
        uint8_t local_addr[SLE_ADDR_LEN] = {0x13, 0x67, 0x5c, 0x07, 0x00, 0x51};
        sle_addr_t local_address;
        local_address.type = 0;
        (void)memcpy_s(local_address.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN);
        sle_set_local_addr(&local_address);
        sle_speed_connect_param_init();
        sle_start_scan();
    }
}

void sle_sample_seek_enable_cbk(errcode_t status)
{
    if (status == 0) {
        return;
    }
}

void sle_sample_seek_disable_cbk(errcode_t status)
{
    if (status == 0) {
        sle_connect_remote_device(&g_remote_addr);
    }
}

void sle_sample_seek_result_info_cbk(sle_seek_result_info_t *seek_result_data)
{
    if (seek_result_data != NULL) {
        uint8_t mac[SLE_ADDR_LEN] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6};
        if (memcmp(seek_result_data->addr.addr, mac, SLE_ADDR_LEN) == 0) {
            (void)memcpy_s(&g_remote_addr, sizeof(sle_addr_t), &seek_result_data->addr, sizeof(sle_addr_t));
            sle_stop_seek();
        }
    }
}

static uint32_t get_float_int(float in)
{
    return (uint32_t)(((uint64_t)(in * SLE_SPEED_HUNDRED)) / SLE_SPEED_HUNDRED);
}

static uint32_t get_float_dec(float in)
{
    return (uint32_t)(((uint64_t)(in * SLE_SPEED_HUNDRED)) % SLE_SPEED_HUNDRED);
}

static void sle_speed_notification_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data,
    errcode_t status)
{
    unused(client_id);
    unused(status);

    if ((g_recv_pkt_num % RSSI_AVG_COUNT) == 0) {
        sle_read_remote_device_rssi(conn_id); // 用于统计rssi均值
    }
    if (g_recv_pkt_num == 0) {
        g_count_before_get_us = uapi_tcxo_get_us();
    } else if (g_recv_pkt_num == RECV_PKT_CNT) {
        g_count_after_get_us = uapi_tcxo_get_us();
        printf("g_count_after_get_us = %llu, g_count_before_get_us = %llu, data_len = %d\r\n",
            g_count_after_get_us, g_count_before_get_us, data->data_len);
        float time = (float)(g_count_after_get_us - g_count_before_get_us) / 1000000.0;  /* 1s = 1000000.0us */
        printf("time = %d.%d s\r\n", get_float_int(time), get_float_dec(time));
        uint16_t len = data->data_len;
        float speed = len * RECV_PKT_CNT * 8 / time;  /* 1B = 8bits */
        printf("speed = %d.%d bps\r\n", get_float_int(speed), get_float_dec(speed));
        g_recv_pkt_num = 0;
        g_count_before_get_us = g_count_after_get_us;
    }
    g_recv_pkt_num++;

     osal_printk("[DTU CALLBACK] sle_speed_notification_cb, conn_id:%d, data_len:%d\r\n", conn_id, data->data_len);
     uapi_uart_write(0, (const uint8_t*)data->data, data->data_len, 0);
     osal_printk("\r\n[DTU CALLBACK] end!!!!\r\n");
}

static void sle_speed_indication_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data,
    errcode_t status)
{
    unused(status);
    unused(conn_id);
    unused(client_id);
    osal_printk("\n sle_speed_indication_cb sle uart recived data : %s\r\n", data->data);
}

void sle_sample_seek_cbk_register(void)
{
    g_seek_cbk.sle_enable_cb = sle_sample_sle_enable_cbk;
    g_seek_cbk.seek_enable_cb = sle_sample_seek_enable_cbk;
    g_seek_cbk.seek_disable_cb = sle_sample_seek_disable_cbk;
    g_seek_cbk.seek_result_cb = sle_sample_seek_result_info_cbk;
}

void sle_sample_connect_state_changed_cbk(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    osal_printk("[ssap client] conn state changed conn_id:%d, addr:0x%02x:**:**:0x%02x:0x%02x\n",
        conn_id, addr->addr[0], addr->addr[4], addr->addr[5]); /* 0 4 5: addr index */
    osal_printk("[ssap client] conn state changed disc_reason:0x%x, pair_state:0x%x, conn_state:0x%x\n",
        disc_reason, pair_state, conn_state);
    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        if (pair_state == SLE_PAIR_NONE) {
            sle_pair_remote_device(&g_remote_addr);
        }
        g_conn_id = conn_id;
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        sle_start_scan();
        g_recv_pkt_num = 0;
    }
}

void sle_sample_auth_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
    const sle_auth_info_evt_t* evt)
{
    unused(conn_id);
    unused(evt);
    osal_printk("[speed server] auth cmp:0x%x\r\n", status);
    if (status == ERRCODE_SLE_SUCCESS) {
        return;
    }
    osal_printk("[speed server] auth failed, remove pair and restart scan\r\n");
    sle_remove_paired_remote_device(addr);
    sle_start_scan();
}

void sle_sample_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    osal_printk("[ssap client] pair complete conn_id:%d, addr:0x%02x:**:**:0x%02x:0x%02x, status:0x%x\n",
        conn_id, addr->addr[0], addr->addr[4], addr->addr[5], status); /* 0 4 5: addr index */
    if (status == ERRCODE_SLE_SUCCESS) {
        ssap_exchange_info_t info = {0};
        info.mtu_size = SLE_MTU_SIZE_DEFAULT;
        info.version = 1;
        ssapc_exchange_info_req(1, g_conn_id, &info);
        return;
    }
    osal_printk("[speed server] pair failed, remove pair and restart scan\r\n");
    sle_remove_paired_remote_device(addr);
    sle_start_scan();
}

void sle_sample_update_cbk(uint16_t conn_id, errcode_t status, const sle_connection_param_update_evt_t *param)
{
    unused(status);
    osal_printk("[ssap client] updat state changed conn_id:%d, interval = 0x%02x\n", conn_id, param->interval);
}

void sle_sample_update_req_cbk(uint16_t conn_id, errcode_t status, const sle_connection_param_update_req_t *param)
{
    unused(conn_id);
    unused(status);
    osal_printk("[ssap client] sle_sample_update_req_cbk interval_min = 0x%02x, interval_max = 0x%02x\n",
        param->interval_min, param->interval_max);
}

void sle_sample_read_rssi_cbk(uint16_t conn_id, int8_t rssi, errcode_t status)
{
    unused(conn_id);
    unused(status);
    g_rssi_sum = g_rssi_sum + rssi;
    g_rssi_number++;
    if (g_rssi_number == RSSI_AVG_COUNT) {
        osal_printk("rssi average = %d dbm\r\n", g_rssi_sum / g_rssi_number);
        g_rssi_sum = 0;
        g_rssi_number = 0;
    }
}

void sle_sample_connect_cbk_register(void)
{
    g_connect_cbk.connect_state_changed_cb = sle_sample_connect_state_changed_cbk;
    g_connect_cbk.auth_complete_cb = sle_sample_auth_complete_cbk;
    g_connect_cbk.pair_complete_cb = sle_sample_pair_complete_cbk;
    g_connect_cbk.connect_param_update_req_cb = sle_sample_update_req_cbk;
    g_connect_cbk.connect_param_update_cb = sle_sample_update_cbk;
    g_connect_cbk.read_rssi_cb = sle_sample_read_rssi_cbk;
}

void sle_sample_exchange_info_cbk(uint8_t client_id, uint16_t conn_id, ssap_exchange_info_t *param,
    errcode_t status)
{
    osal_printk("[ssap client] pair complete client id:%d status:%d\n", client_id, status);
    osal_printk("[ssap client] exchange mtu, mtu size: %d, version: %d.\n",
        param->mtu_size, param->version);

    ssapc_find_structure_param_t find_param = {0};
    find_param.type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    ssapc_find_structure(0, conn_id, &find_param);
}

void sle_sample_find_structure_cbk(uint8_t client_id, uint16_t conn_id, ssapc_find_service_result_t *service,
    errcode_t status)
{
    osal_printk("[ssap client] find structure cbk client: %d conn_id:%d status: %d \n",
        client_id, conn_id, status);
    osal_printk("[ssap client] find structure start_hdl:[0x%02x], end_hdl:[0x%02x], uuid len:%d\r\n",
        service->start_hdl, service->end_hdl, service->uuid.len);
    if (service->uuid.len == UUID_16BIT_LEN) {
        osal_printk("[ssap client] structure uuid:[0x%02x][0x%02x]\r\n",
            service->uuid.uuid[14], service->uuid.uuid[15]); /* 14 15: uuid index */
    } else {
        for (uint8_t idx = 0; idx < UUID_128BIT_LEN; idx++) {
            osal_printk("[ssap client] structure uuid[%d]:[0x%02x]\r\n", idx, service->uuid.uuid[idx]);
        }
    }
    g_find_service_result.start_hdl = service->start_hdl;
    g_find_service_result.end_hdl = service->end_hdl;
    if (memcpy_s(&g_find_service_result.uuid, sizeof(sle_uuid_t), &service->uuid, sizeof(sle_uuid_t)) != EOK) {
        osal_printk("[ssap client] find structure mem cpy failed");
        return;
    }
    my_handle=service->start_hdl;
}

void sle_sample_find_structure_cmp_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_find_structure_result_t *structure_result, errcode_t status)
{
    osal_printk("[ssap client] find structure cmp cbk client id:%d status:%d type:%d uuid len:%d \r\n",
        client_id, status, structure_result->type, structure_result->uuid.len);
    if (structure_result->uuid.len == UUID_16BIT_LEN) {
        osal_printk("[ssap client] find structure cmp cbk structure uuid:[0x%02x][0x%02x]\r\n",
            structure_result->uuid.uuid[14], structure_result->uuid.uuid[15]); /* 14 15: uuid index */
    } else {
        for (uint8_t idx = 0; idx < UUID_128BIT_LEN; idx++) {
            osal_printk("[ssap client] find structure cmp cbk structure uuid[%d]:[0x%02x]\r\n", idx,
                structure_result->uuid.uuid[idx]);
        }
    }
    uint8_t data[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t len = sizeof(data);
    ssapc_write_param_t param = {0};
    param.handle = g_find_service_result.start_hdl;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.data_len = len;
    param.data = data;
    ssapc_write_req(0, conn_id, &param);
}

void sle_sample_find_property_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_find_property_result_t *property, errcode_t status)
{
    osal_printk("[ssap client] find property cbk, client id: %d, conn id: %d, operate ind: %d, "
        "descriptors count: %d status:%d.\n", client_id, conn_id, property->operate_indication,
        property->descriptors_count, status);
    for (uint16_t idx = 0; idx < property->descriptors_count; idx++) {
        osal_printk("[ssap client] find property cbk, descriptors type [%d]: 0x%02x.\n",
            idx, property->descriptors_type[idx]);
    }
    if (property->uuid.len == UUID_16BIT_LEN) {
        osal_printk("[ssap client] find property cbk, uuid: 0x%02x %02x.\n",
            property->uuid.uuid[14], property->uuid.uuid[15]); /* 14 15: uuid index */
    } else if (property->uuid.len == UUID_128BIT_LEN) {
        for (uint16_t idx = 0; idx < UUID_128BIT_LEN; idx++) {
            osal_printk("[ssap client] find property cbk, uuid [%d]: 0x%02x.\n",
                idx, property->uuid.uuid[idx]);
        }
    }
    // my_handle = property->handle;

}

void sle_sample_write_cfm_cbk(uint8_t client_id, uint16_t conn_id, ssapc_write_result_t *write_result,
    errcode_t status)
{
    unused(conn_id);
    unused(write_result);
    osal_printk("[ssap client] write cfm cbk, client id: %d status:%d.\n", client_id, status);
    // server端在接收到read_req消息后，会创建一个发包线程
    // ssapc_read_req(0, conn_id, write_result->handle, write_result->type);
}

void sle_sample_read_cfm_cbk(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *read_data,
    errcode_t status)
{
    unused(client_id);
    unused(conn_id);
    unused(read_data);
    unused(status);
    // osal_printk("[ssap client] read cfm cbk client id: %d conn id: %d status: %d\n",
    //     client_id, conn_id, status);
    // osal_printk("[ssap client] read cfm cbk handle: %d, type: %d , len: %d\n",
    //     read_data->handle, read_data->type, read_data->data_len);
    // for (uint16_t idx = 0; idx < read_data->data_len; idx++) {
    //     osal_printk("[ssap client] read cfm cbk[%d] 0x%02x\r\n", idx, read_data->data[idx]);
    // }
}

void sle_sample_ssapc_cbk_register(ssapc_notification_callback notification_cb,
    ssapc_notification_callback indication_cb)
{
    g_ssapc_cbk.exchange_info_cb = sle_sample_exchange_info_cbk;
    g_ssapc_cbk.find_structure_cb = sle_sample_find_structure_cbk;
    g_ssapc_cbk.find_structure_cmp_cb = sle_sample_find_structure_cmp_cbk;
    g_ssapc_cbk.ssapc_find_property_cbk = sle_sample_find_property_cbk;
    g_ssapc_cbk.notification_cb = notification_cb;
    g_ssapc_cbk.indication_cb = indication_cb;
}

void sle_client_init(ssapc_notification_callback notification_cb, ssapc_indication_callback indication_cb)
{
    sle_sample_seek_cbk_register();
    sle_sample_connect_cbk_register();
    sle_sample_ssapc_cbk_register(notification_cb, indication_cb);
    sle_announce_seek_register_callbacks(&g_seek_cbk);
    sle_connection_register_callbacks(&g_connect_cbk);
    ssapc_register_callbacks(&g_ssapc_cbk);
    enable_sle();
}


static void dtu_uart_rx_callback(const void *buffer, uint16_t length, bool error)
{
    unused(error);
    ssapc_write_param_t param = {0};
    errcode_t ret;


    /* 限制数据长度 */
    if (length > sizeof(g_app_uart_tx_buff)) {
        length = sizeof(g_app_uart_tx_buff);
    }

    /* ✅ 关键修复：拷贝数据到自己的缓冲区 */
    /* 因为 ssapc_write_req 是异步的，不能直接使用 UART 驱动的缓冲 */
    if (memcpy_s(g_app_uart_tx_buff, sizeof(g_app_uart_tx_buff), (uint8_t*)buffer, length) != EOK) {
        osal_printk("[DTU UART] memcpy failed\r\n");
        return;
    }

    /* 准备写参数 */
     param.handle = g_find_service_result.end_hdl;
    // param.handle = my_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.data_len = length;
    param.data = g_app_uart_tx_buff;  /* 使用自己的缓冲 */

       ret = ssapc_write_req(0, g_conn_id, &param);
    osal_printk("[DTU UART] rx %d bytes, ssapc_write_req ret=0x%x conn=%u handle=0x%x\r\n",
        length, ret, g_conn_id, param.handle);

}

void uart_test_init(void)
{
    uart_attr_t attr = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };
    uart_extra_attr_t extra_attr = {
        .tx_dma_enable = false,
        .tx_int_threshold = UART_FIFO_INT_TX_LEVEL_EQ_0_CHARACTER,
        .rx_dma_enable = false,
        .rx_int_threshold = UART_FIFO_INT_RX_LEVEL_1_4
    };

    uart_pin_config_t pin_config = {
        .tx_pin = 17,
        .rx_pin = 18,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };

    static uart_buffer_config_t g_app_uart_buffer_config = {
        .rx_buffer = g_app_uart_rx_buff,
        .rx_buffer_size = 128
    };
    errcode_t ret;

    /* 初始化 DMA 模块 */
    uapi_dma_init();
    uapi_dma_open();

    /* 初始化信号量 */
    ret = osal_sem_binary_sem_init(&g_app_uart_rx_sem, 0);
    if (ret != OSAL_SUCCESS) {
        osal_printk("[DTU UART] sem init failed: 0x%x\r\n", ret);
        return;
    }

    uapi_pin_set_mode(17, 1);
    uapi_pin_set_mode(18, 1);

    uapi_uart_deinit(0);
    ret = uapi_uart_init(0, &pin_config, &attr, &extra_attr, &g_app_uart_buffer_config);
    osal_printk("[DTU UART] init ret=0x%x bus=0 tx=17 rx=18 baud=115200\r\n", ret);
    if (ret != ERRCODE_SUCC) {
        return;
    }

    // uapi_uart_unregister_rx_callback(0);
    ret = uapi_uart_register_rx_callback(0, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 1, dtu_uart_rx_callback);
    osal_printk("[DTU UART] register rx cb ret=0x%x\r\n", ret);
}




int sle_speed_init(void)
{
    osal_msleep(1000);  /* sleep 1000ms */
    sle_client_init(sle_speed_notification_cb, sle_speed_indication_cb);
    uart_test_init();
    return 0;
}

#define SLE_SPEED_TASK_PRIO 26
#define SLE_SPEED_STACK_SIZE 0x2000
static void sle_speed_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)sle_speed_init, 0, "ThroughputTask", SLE_SPEED_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, SLE_SPEED_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

/* Run the blinky_entry. */
app_run(sle_speed_entry);
