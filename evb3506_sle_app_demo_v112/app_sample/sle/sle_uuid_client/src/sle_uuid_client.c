/**
 * Copyright (c) @CompanyNameMagicTag 2022. All rights reserved.
 *
 * Description: SLE private service register sample of client.
 */
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sched.h>
#include "securec.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_device_manager.h"
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "sle_errcode.h"
#include "soc_osal.h"
#include "sle_transmition_manager.h"
#include "sle_uuid_client.h"

#undef THIS_FILE_ID
#define THIS_FILE_ID BTH_GLE_SAMPLE_UUID_CLIENT

#define SLE_DATA_SIZE_DEFAULT            200  // 73E可使用1450
#define SLE_SEEK_INTERVAL_DEFAULT        100
#define SLE_SEEK_WINDOW_DEFAULT          100
#define UUID_16BIT_LEN                   2
#define UUID_128BIT_LEN                  16
#define SLE_UUID_SPEED_NTF_REPORT_H      0x11
#define SLE_UUID_SPEED_NTF_REPORT_L      0x22
#define SLE_UUID_APP_SAMPLE_RX_H         0x23
#define SLE_UUID_APP_SAMPLE_RX_L         0x23
#define SPEED_DEFAULT_CONN_INTERVAL      0x14
#define SPEED_DEFAULT_TIMEOUT_MULTIPLIER 0x1f4
#define DEFAULT_SLE_SPEED_MTU_SIZE       1500
#define SPEED_DEFAULT_SCAN_INTERVAL      400
#define SPEED_DEFAULT_SCAN_WINDOW        20

#define MS_100                           100000
#define THOUSAND                         1000
#define UUID_14_BYTE                     14
#define UUID_15_BYTE                     15
#define SLE_SEND_DATA_DELAY_SLOT_TIME    2 /* SPEED_DEFAULT_CONN_INTERVAL * SLE_SLOT, SLE_SLOT是0.125ms */
#define SLE_SEND_DATA_TRY_TIMES          10
#define SLE_WAIT_TIME 20 // 20 *10ms = 200ms 超时时间

#define sample_at_log_print(fmt, args...) printf(fmt, ##args)

sle_announce_seek_callbacks_t g_seek_cbk = {0};
sle_connection_callbacks_t g_connect_cbk = {0};
ssapc_callbacks_t g_ssapc_cbk = {0};
static uint8_t g_sle_enable_status = 0;
static sle_uuid_t g_client_app_uuid = {UUID_16BIT_LEN, {0}};
static uint8_t g_client_id = 0;
static uint16_t g_connect_id = 0;
static uint16_t g_handle = 0;
static sle_send_data_state_t g_send_flag = SEND_STATE_IDLE;
static sle_addr_t g_addr = {0};
static pthread_t g_send_thread = -1;

static void sle_request_exchange_info(uint16_t conn_id)
{
    ssap_exchange_info_t info = {0};
    info.mtu_size = DEFAULT_SLE_SPEED_MTU_SIZE;
    info.version = 1;
    errcode_t ret = ssapc_exchange_info_req(g_client_id, conn_id, &info);
    sample_at_log_print("[ssap client] exchange info req ret: %d\r\n", ret);
}

static void sle_speed_connect_param_init(void)
{
    sle_default_connect_param_t param = {0};
    param.enable_filter_policy = 0;
    param.gt_negotiate = SLE_ANNOUNCE_ROLE_G_CAN_NEGO;
    param.initiate_phys = SLE_SEEK_PHY_1M;
    param.max_interval = SPEED_DEFAULT_CONN_INTERVAL;
    param.min_interval = SPEED_DEFAULT_CONN_INTERVAL;
    param.scan_interval = SPEED_DEFAULT_SCAN_INTERVAL;
    param.scan_window = SPEED_DEFAULT_SCAN_WINDOW;
    param.timeout = SPEED_DEFAULT_TIMEOUT_MULTIPLIER;
    sle_default_connection_param_set(&param);
}

unsigned long M_TIME_TO_USEC(unsigned long tv_sec, unsigned long tv_usec)
{
    return (tv_sec * THOUSAND * THOUSAND + tv_usec);
}

void msdelay(int msec)
{
    osal_timeval pre_time = { 0 };
    osal_timeval cur_time = { 0 };
    osal_gettimeofday(&pre_time);
    unsigned long pre_us = M_TIME_TO_USEC(pre_time.tv_sec, pre_time.tv_usec);
    while (1) {
        osal_gettimeofday(&cur_time);
        if (M_TIME_TO_USEC(cur_time.tv_sec, cur_time.tv_usec) - pre_us > msec * THOUSAND) {
            break;
        }
    }
}

static void sle_sample_init_data(uint8_t *data)
{
    sample_at_log_print("[ssap client] init data\r\n");
    for (int i = 0; i < SLE_DATA_SIZE_DEFAULT; i++) {
        data[i] = i; // 这里仅为示例，将所有元素初始化为它们的索引值
    }
}

static void sle_sample_send_data(uint8_t *data, uint16_t data_len)
{
    ssapc_write_param_t sle_send_param = {0};
    sle_send_param.handle = g_handle;
    sle_send_param.type = SSAP_PROPERTY_TYPE_VALUE;
    sle_send_param.data_len = data_len;
    sle_send_param.data = data;
    errcode_t ret = ERRCODE_SLE_SUCCESS;
    uint32_t i;
    for (i = 0; i < SLE_SEND_DATA_TRY_TIMES; i++) {
        ret = ssapc_write_cmd(g_client_id, g_connect_id, &sle_send_param);
        if (ret != ERRCODE_SLE_SUCCESS) {
            msdelay(SLE_SEND_DATA_DELAY_SLOT_TIME);
        } else {
            msdelay(SLE_SEND_DATA_DELAY_SLOT_TIME);
            break;
        }
    }
}

static void *sle_send_thread(void *arg)
{
    uint8_t data[SLE_DATA_SIZE_DEFAULT] = {0};
    sle_sample_init_data(data);

    while (1) {
        // 0还未初始化完成，等待
        if (g_send_flag == SEND_STATE_IDLE) {
            usleep(MS_100);
            continue;
        }
        // 2退出
        if (g_send_flag == SEND_STATE_EXIT) {
            break;
        }
        sle_sample_send_data(data, SLE_DATA_SIZE_DEFAULT);
    }
    return NULL;
}

int sle_send_thread_init()
{
    int ret = pthread_create(&g_send_thread, NULL, sle_send_thread, NULL);
    if (ret < 0) {
        sample_at_log_print("Err: fail to create write thread.\n");
        return -1;
    }
    sample_at_log_print("sle_comm_init.\n");
    return 0;
}

void sle_send_thread_deinit()
{
    g_send_flag = SEND_STATE_EXIT;
    pthread_join(g_send_thread, NULL);
}

void sle_sample_send_thread_reset()
{
    g_send_flag = SEND_STATE_IDLE;
}

void sle_wait_result_handle(uint8_t timeout, uint8_t *wait_flag, uint8_t wait_value)
{
    uint8_t i;
    for (i = 0; i < timeout; i++) {
        if ((*wait_flag) == wait_value) {
            break;
        }
        usleep(MS_100);
    }
}

void sle_sample_sle_enable_cbk(errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("sle sample sle enable failed, status: 0x%02x", status);
        return;
    }
    g_sle_enable_status = 1;
    printf("sle sample sle enable success.\r\n");
    sle_speed_connect_param_init();
    sle_start_scan();
    errcode_t ret = ssapc_register_client(&g_client_app_uuid, &g_client_id);
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("gattc_register_client failed, errcode: %d\r\n", ret);
        return;
    }
}

void sle_sample_seek_enable_cbk(errcode_t status)
{
    sample_at_log_print("sle sample seek enable status: 0x%02x.\r\n", status);
}

void sle_sample_seek_disable_cbk(errcode_t status)
{
    g_sle_enable_status = 0;
    printf("sle sample seek disable status: %02x.\r\n", status);
    if (status != ERRCODE_SLE_SUCCESS) {
        return;
    }
    sle_connect_remote_device(&g_addr);
}

void sle_sample_seek_result_info_cbk(sle_seek_result_info_t *seek_result_data)
{
    static uint8_t server_addr[SLE_ADDR_LEN] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6};  // server MAC
    sample_at_log_print("sle_sample_seek_result_info_cbk enter.\r\n");
    if (seek_result_data != NULL) {
        sample_at_log_print("find device, checking MAC address...\r\n");
        if (!memcmp(server_addr, seek_result_data->addr.addr, (sizeof(server_addr) / sizeof(server_addr[0])))) {
            sle_stop_seek();
            sample_at_log_print("find SLE_SERVER_ADDR device.\r\n");
            usleep(MS_100);
            memcpy_s(&g_addr, sizeof(sle_addr_t), &(seek_result_data->addr), sizeof(sle_addr_t));
        }
    }
}

void sle_sample_seek_cbk_register(void)
{
    g_seek_cbk.sle_enable_cb = sle_sample_sle_enable_cbk;
    g_seek_cbk.seek_enable_cb = sle_sample_seek_enable_cbk;
    g_seek_cbk.seek_disable_cb = sle_sample_seek_disable_cbk;
    g_seek_cbk.seek_result_cb = sle_sample_seek_result_info_cbk;
}

void sle_sample_connect_state_changed_cbk(uint16_t conn_id, const sle_addr_t *addr, sle_acb_state_t conn_state,
    sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_connect_id = conn_id;

        if (pair_state == SLE_PAIR_NONE) {
            sample_at_log_print("pair remote device.\r\n");
            sle_pair_remote_device(addr);
        } else {
            sle_request_exchange_info(conn_id);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        sample_at_log_print("SLE disconnect. reason=0x%x\n", disc_reason);
        sle_sample_send_thread_reset();
        sle_start_scan();
    }
}

void sle_sample_auth_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
    const sle_auth_info_evt_t *evt)
{
    (void)conn_id;
    (void)evt;
    sample_at_log_print("[ssap client] auth complete status: 0x%x\r\n", status);
    if (status == ERRCODE_SLE_SUCCESS) {
        return;
    }
    sample_at_log_print("[ssap client] auth failed, remove pair and restart scan\r\n");
    sle_remove_paired_remote_device(addr);
    sle_start_scan();
}

void sle_sample_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    sample_at_log_print("sle_sample_pair_complete_cbk status: %d\r\n", status);
    if (status == ERRCODE_SLE_SUCCESS) {
        sle_request_exchange_info(conn_id);
        return;
    }
    sample_at_log_print("[ssap client] pair failed, remove pair and restart scan\r\n");
    sle_remove_paired_remote_device(addr);
    sle_start_scan();
}

void sle_sample_update_cbk(uint16_t conn_id, errcode_t status, const sle_connection_param_update_evt_t *param)
{
    sample_at_log_print("[ssap client] updat state changed status %d conn_id:%d, interval = %02x\n",
           status, conn_id, param->interval);
}

void sle_sample_set_phy_cbk(uint16_t conn_id, errcode_t status, const sle_set_phy_t *param)
{
    sample_at_log_print("[ssap client] sle_sample_set_phy_cbk\r\n");
    (void)status;
    (void)param;
}

void sle_sample_read_rssi_cbk(uint16_t conn_id, int8_t rssi, errcode_t status)
{
    sample_at_log_print("RSSI: %d    connect ID: %d\r\n", rssi, g_connect_id);
}

void sle_sample_connect_cbk_register(void)
{
    g_connect_cbk.connect_state_changed_cb = sle_sample_connect_state_changed_cbk;
    g_connect_cbk.auth_complete_cb = sle_sample_auth_complete_cbk;
    g_connect_cbk.pair_complete_cb = sle_sample_pair_complete_cbk;
    g_connect_cbk.connect_param_update_cb = sle_sample_update_cbk;
    g_connect_cbk.set_phy_cb = sle_sample_set_phy_cbk;
    g_connect_cbk.read_rssi_cb = sle_sample_read_rssi_cbk;
}

void sle_sample_exchange_info_cbk(uint8_t client_id, uint16_t conn_id, ssap_exchange_info_t *param, errcode_t status)
{
    sample_at_log_print("[ssap client] sle_sample_exchange_info_cbk client:%d status:%d mtu:%d version:%d\r\n",
        client_id, status, param->mtu_size, param->version);
    // 服务发现
    ssapc_find_structure_param_t find_param = {0};
    find_param.type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    ssapc_find_structure(0, conn_id, &find_param);
}

void sle_sample_find_structure_cbk(
    uint8_t client_id, uint16_t conn_id, ssapc_find_service_result_t *service, errcode_t status)
{
    sample_at_log_print("[ssap client] find structure cbk client: %d conn_id:%d status: %d \n",
        client_id, conn_id, status);
    sample_at_log_print("[ssap client] find structure start_hdl:[0x%02x], end_hdl:[0x%02x], uuid len:%d\r\n",
        service->start_hdl, service->end_hdl, service->uuid.len);

    ssapc_find_structure_param_t find_param = {0};
    find_param.type = SSAP_FIND_TYPE_PROPERTY;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    ssapc_find_structure(0, conn_id, &find_param);
}

void sle_sample_find_structure_cmp_cbk(
    uint8_t client_id, uint16_t conn_id, ssapc_find_structure_result_t *structure_result, errcode_t status)
{
    sample_at_log_print("[ssap client] find structure cmp cbk client id:%d status:%d type:%d uuid len:%d \r\n",
        client_id,
        status,
        structure_result->type,
        structure_result->uuid.len);
}

void sle_sample_find_property_cbk(
    uint8_t client_id, uint16_t conn_id, ssapc_find_property_result_t *property, errcode_t status)
{
    sample_at_log_print("[ssap client] find property cbk, client id: %d, conn id: %d, operate ind: %d, "
           "descriptors count: %d status:%d.\n",
        client_id,
        conn_id,
        property->operate_indication,
        property->descriptors_count,
        status);

    sample_at_log_print("[ssap client] property handle:0x%02x uuid:0x%02x%02x\r\n",
        property->handle, property->uuid.uuid[UUID_14_BYTE], property->uuid.uuid[UUID_15_BYTE]);

    if (property->uuid.uuid[UUID_14_BYTE] == SLE_UUID_SPEED_NTF_REPORT_H &&
        property->uuid.uuid[UUID_15_BYTE] == SLE_UUID_SPEED_NTF_REPORT_L) {
        sample_at_log_print("[ssap client] find speed notify property, trigger server notify.\r\n");
        g_handle = property->handle;
        errcode_t ret = ssapc_read_req(client_id, conn_id, property->handle, SSAP_PROPERTY_TYPE_VALUE);
        sample_at_log_print("[ssap client] read req ret: %d\r\n", ret);
    } else if (property->uuid.uuid[UUID_14_BYTE] == SLE_UUID_APP_SAMPLE_RX_H &&
        property->uuid.uuid[UUID_15_BYTE] == SLE_UUID_APP_SAMPLE_RX_L) {
        sample_at_log_print("[ssap client] find app sample rx property, begin sending data.\r\n");
        g_handle = property->handle;
        g_send_flag = SEND_STATE_SENDING;
    } else {
        sample_at_log_print("[ssap client] unmatched property uuid:");
        for (uint16_t idx = 0; idx < property->uuid.len; idx++) {
            sample_at_log_print("%x", property->uuid.uuid[idx]);
        }
        sample_at_log_print("\r\n");
    }
}

void sle_sample_write_cfm_cbk(uint8_t client_id, uint16_t conn_id, ssapc_write_result_t *write_result, errcode_t status)
{
    sample_at_log_print("[ssap client] write cfm cbk, client id: %d status:%d.\n", client_id, status);
}

void sle_sample_read_cfm_cbk(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *read_data, errcode_t status)
{
    sample_at_log_print("[ssap client] read cfm cbk client id: %d conn id: %d status: 0x%02x\n",
        client_id, conn_id, status);
    sample_at_log_print("[ssap client] read cfm cbk handle: %d, type: %d , len: %d\n",
        read_data->handle, read_data->type, read_data->data_len);
    for (uint16_t idx = 0; idx < read_data->data_len; idx++) {
        sample_at_log_print("[ssap client] read cfm cbk[%d] 0x%02x\r\n", idx, read_data->data[idx]);
    }
}

void sle_notification_cbk(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status)
{
    sample_at_log_print("[ssap client] sle_notification_cbk, client id: %d conn id: %d status: %d, data is \n",
        client_id, conn_id, status);
    for (uint8_t i = 0; i < data->data_len; i++) {
        sample_at_log_print("0x%x ", data->data[i]);
    }
    sample_at_log_print("\r\n");
}

void sle_sample_ssapc_cbk_register(void)
{
    g_ssapc_cbk.exchange_info_cb = sle_sample_exchange_info_cbk;
    g_ssapc_cbk.find_structure_cb = sle_sample_find_structure_cbk;
    g_ssapc_cbk.find_structure_cmp_cb = sle_sample_find_structure_cmp_cbk;
    g_ssapc_cbk.ssapc_find_property_cbk = sle_sample_find_property_cbk;
    g_ssapc_cbk.write_cfm_cb = sle_sample_write_cfm_cbk;
    g_ssapc_cbk.read_cfm_cb = sle_sample_read_cfm_cbk;
    g_ssapc_cbk.notification_cb = sle_notification_cbk;
}

void sle_start_scan(void)
{
    sle_seek_param_t param = {0};
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = SLE_SEEK_FILTER_ALLOW_ALL;
    param.seek_phys = SLE_SEEK_PHY_1M;
    param.seek_type[0] = SLE_SEEK_ACTIVE;
    param.seek_interval[0] = SLE_SEEK_INTERVAL_DEFAULT;
    param.seek_window[0] = SLE_SEEK_WINDOW_DEFAULT;
    sle_set_seek_param(&param);

    sle_start_seek();
    return;
}

static errcode_t sle_sample_register_persistence_info()
{
    char buff[BYTE_LEN_128] = {0};
    if (getcwd(buff, BYTE_LEN_128) == NULL) {
        sample_at_log_print("get pwd error:0x%x", errno);
        return ERRCODE_SLE_FAIL;
    }
    sample_at_log_print("%s current path: %s", __func__, buff);
    return sle_dev_manager_register_file_path((const uint8_t *)buff);
}

void sle_client_init()
{
    sle_sample_seek_cbk_register();
    sle_sample_connect_cbk_register();
    sle_sample_ssapc_cbk_register();
    sle_announce_seek_register_callbacks(&g_seek_cbk);
    sle_connection_register_callbacks(&g_connect_cbk);
    ssapc_register_callbacks(&g_ssapc_cbk);
    errcode_t ret = sle_sample_register_persistence_info();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("%s reg persistence_info failed, errcode: %d\n", __func__, ret);
    }
    ret = enable_sle();
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("enable_sle failed, errorcode: %0x\r\n", ret);
        return;
    }
    sle_wait_result_handle(SLE_WAIT_TIME, &g_sle_enable_status, 1);
    sle_send_thread_init();
    return;
}

void sle_client_deinit()
{
    sle_send_thread_deinit();
    errcode_t ret = ERRCODE_SLE_SUCCESS;
    sle_stop_seek();
    ret |= ssapc_unregister_client(g_client_id);  // 去注册
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("ssapc_unregister_client failed, errorcode: %0x\r\n", ret);
        return;
    }
    ret |= disable_sle();
    if (ret != ERRCODE_SLE_SUCCESS) {
        sample_at_log_print("disable_sle failed, errorcode: %0x\r\n", ret);
        return;
    }
    sle_wait_result_handle(SLE_WAIT_TIME, &g_sle_enable_status, 0);
    return;
}
