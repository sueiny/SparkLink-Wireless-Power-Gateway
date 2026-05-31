/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE broadcast mesh network internal definitions.
 */

#ifndef SLE_MESH_INTERNAL_H
#define SLE_MESH_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "common_def.h"
#include "errcode.h"
#include "sle_common.h"
#include "soc_osal.h"
#include "sle_device_discovery.h"
#include "SM_mesh.h"

/*--------------------------------------------------------------------------
 * Protocol constants / 协议常数
 *--------------------------------------------------------------------------*/
#define SM_MAGIC0                           0x4D    /* 'M' */
#define SM_MAGIC1                           0x53    /* 'S' */
#define SM_PROTO_VERSION                    0x01

/*--------------------------------------------------------------------------
 * Frame types / 帧类型
 *--------------------------------------------------------------------------*/
typedef enum {
    SM_FRAME_TYPE_RSSI_REPORT   = 0x01,
    SM_FRAME_TYPE_DATA          = 0x02,
    SM_FRAME_TYPE_PATH_REQ      = 0x03,
    SM_FRAME_TYPE_PATH_RESP     = 0x04,
    SM_FRAME_TYPE_ACK           = 0x05,
} sm_frame_type_t;

/*--------------------------------------------------------------------------
 * Capacity limits / 容量上限
 *--------------------------------------------------------------------------*/
#define SM_ADV_HANDLE                       1
#define SM_ADV_DATA_LEN_MAX                 200
#define SM_MAX_FRAME_LEN                    200
#define SM_MAX_NEIGHBORS                    32
#define SM_DEDUP_TABLE_SIZE                 32
#define SM_PATH_CACHE_SIZE                  8
#define SM_RELAY_QUEUE_SIZE                 8

/*--------------------------------------------------------------------------
 * Address constants / 地址常数
 *--------------------------------------------------------------------------*/
#define SM_DONGLE_ADDR                      0x00
#define SM_NODE_ADDR_INVALID                0xFF

/*--------------------------------------------------------------------------
 * Timing constants (ms) / 定时常数（毫秒）
 *--------------------------------------------------------------------------*/
#define SM_RSSI_REPORT_PERIOD_MS            5000
#define SM_ADV_DURATION_MS                  125
#define SM_SCAN_COLLECT_WINDOW_MS           3000
#define SM_NEIGHBOR_TIMEOUT_MS              15000
#define SM_DEDUP_TIMEOUT_MS                 2000
#define SM_WORKER_SLEEP_MS                  50

/*--------------------------------------------------------------------------
 * Advertising parameters / 广播参数
 *--------------------------------------------------------------------------*/
#define SM_ADV_INTERVAL_MIN                 0xC8    /* 25ms * 0.125 = 25ms */
#define SM_ADV_INTERVAL_MAX                 0xC8
#define SM_ADV_TX_POWER                     14
#define SM_ADV_CHANNEL_MAP                  0x07

/*--------------------------------------------------------------------------
 * Scan parameters / 扫描参数
 *--------------------------------------------------------------------------*/
#define SM_SCAN_INTERVAL                    160
#define SM_SCAN_WINDOW                      48

/*--------------------------------------------------------------------------
 * UART constants / UART 常数
 *--------------------------------------------------------------------------*/
#define SM_UART_BUS_ID                      0
#define SM_UART_RX_RING_SIZE                512
#define SM_UART_LINE_MAX                    256

/*--------------------------------------------------------------------------
 * Task parameters / 任务参数
 *--------------------------------------------------------------------------*/
#define SM_TASK_STACK_SIZE                  0x2400
#define SM_TASK_PRIO                        18

/*--------------------------------------------------------------------------
 * Neighbor entry / 邻居表项
 *--------------------------------------------------------------------------*/
typedef struct {
    bool in_use;
    uint8_t addr;
    int8_t rssi;
    uint32_t last_seen_ms;
} sm_neighbor_t;

/*--------------------------------------------------------------------------
 * Dedup entry (relay loop prevention) / 去重表项（防中继环路）
 *--------------------------------------------------------------------------*/
typedef struct {
    bool in_use;
    uint8_t frame_type;
    uint8_t src_addr;
    uint16_t seq;
    uint32_t seen_ms;
} sm_dedup_entry_t;

/*--------------------------------------------------------------------------
 * RSSI collection entry (dongle aggregation) / RSSI 收集表项（dongle 聚合）
 *--------------------------------------------------------------------------*/
#define SM_RSSI_MAX_ENTRIES             16
#define SM_RSSI_COLLECT_WINDOW_MS       200
#define SM_RSSI_STORE_SLOTS             16

typedef struct {
    uint8_t addr;
    int32_t rssi_sum;
    uint16_t count;
} sm_rssi_entry_t;

typedef struct {
    bool active;
    uint8_t src_addr;
    uint32_t first_ms;
    sm_rssi_entry_t entries[SM_RSSI_MAX_ENTRIES];
    uint8_t entry_count;
} sm_rssi_collect_t;

/* RSSI stored report (averaged) / 存储的 RSSI 报告（已平均） */
typedef struct {
    uint8_t src_addr;
    uint8_t count;
    sm_rssi_entry_t entries[SM_RSSI_MAX_ENTRIES];
} sm_rssi_store_entry_t;

/*--------------------------------------------------------------------------
 * Path cache entry / 路径缓存表项
 *--------------------------------------------------------------------------*/
typedef struct {
    bool in_use;
    uint8_t dst_addr;
    uint8_t path_len;
    uint8_t path[SLE_ADDR_LEN];     /* max 6 hops */
    uint32_t stored_ms;
} sm_path_cache_entry_t;

/*--------------------------------------------------------------------------
 * Frame view (parsed frame) / 帧视图（解析后的帧）
 *--------------------------------------------------------------------------*/
typedef struct {
    uint8_t frame_type;
    uint8_t src_addr;
    uint8_t dst_addr;
    uint16_t seq;
    uint8_t count;              /* RSSI_REPORT: neighbor count */
    uint8_t path_len;
    uint8_t current_hop;
    const uint8_t *path;
    uint16_t payload_len;
    const uint8_t *payload;     /* RSSI_REPORT: points to [addr,rssi] entries */
} sm_frame_view_t;

/*--------------------------------------------------------------------------
 * Global context / 全局上下文
 *--------------------------------------------------------------------------*/
typedef struct {
    /* Identity / 身份 */
    uint8_t local_addr;
    bool is_dongle;

    /* SLE state / SLE 状态 */
    bool sle_enabled;
    bool announce_started;

    /* Neighbor table / 邻居表 */
    sm_neighbor_t neighbors[SM_MAX_NEIGHBORS];

    /* Dedup table / 去重表 */
    sm_dedup_entry_t dedup[SM_DEDUP_TABLE_SIZE];

    /* Path cache / 路径缓存 */
    sm_path_cache_entry_t path_cache[SM_PATH_CACHE_SIZE];

    /* Sequence counter / 序列号计数器 */
    uint16_t next_seq;

    /* Timing / 定时 */
    uint32_t last_rssi_report_ms;
    uint32_t adv_started_ms;

    /* Relay queue (circular) / 中继队列（循环） */
    uint8_t relay_queue[SM_RELAY_QUEUE_SIZE][SM_MAX_FRAME_LEN];
    uint16_t relay_lens[SM_RELAY_QUEUE_SIZE];
    uint8_t relay_head;
    uint8_t relay_tail;

    /* UART ring buffer (dongle) / UART 环形缓冲 */
    uint8_t uart_rx_ring[SM_UART_RX_RING_SIZE];
    uint16_t uart_rx_read_pos;
    uint16_t uart_rx_write_pos;
    char uart_line[SM_UART_LINE_MAX];
    uint16_t uart_line_len;

    /* Dongle RSSI collection / dongle RSSI 聚合 */
    sm_rssi_collect_t rssi_collect;

    /* Dongle RSSI stored reports / dongle RSSI 存储报告 */
    sm_rssi_store_entry_t rssi_store[SM_RSSI_STORE_SLOTS];
    uint8_t rssi_store_count;

    /* Dongle topology collection / dongle 拓扑收集 */
    uint8_t topo_reports[SM_MAX_NEIGHBORS][SM_MAX_FRAME_LEN];
    uint16_t topo_report_lens[SM_MAX_NEIGHBORS];
    uint8_t topo_report_count;
} sm_ctx_t;

extern sm_ctx_t g_sm_ctx;

/*--------------------------------------------------------------------------
 * Module: SM_mesh_proto.c / 帧编解码
 *--------------------------------------------------------------------------*/
uint16_t sm_get_le16(const uint8_t *p);
void sm_put_le16(uint8_t *p, uint16_t v);
bool sm_parse_frame(const uint8_t *data, uint16_t data_len, sm_frame_view_t *view);
uint16_t sm_build_rssi_report(uint8_t *buf, uint16_t max_len, uint8_t src_addr);
uint16_t sm_build_path_req(uint8_t *buf, uint16_t max_len, uint8_t src_addr, uint8_t dst_addr, uint16_t seq);
uint16_t sm_build_path_resp(uint8_t *buf, uint16_t max_len, uint8_t src_addr, uint8_t dst_addr,
    uint16_t seq, const uint8_t *path, uint8_t path_len);
uint16_t sm_build_data_frame(uint8_t *buf, uint16_t max_len, uint8_t src_addr, uint8_t dst_addr,
    uint16_t seq, const uint8_t *path, uint8_t path_len, uint8_t current_hop,
    const uint8_t *payload, uint16_t payload_len);
uint16_t sm_build_ack(uint8_t *buf, uint16_t max_len, uint8_t src_addr, uint16_t seq);

/*--------------------------------------------------------------------------
 * Module: SM_mesh_adv.c / 广播管理
 *--------------------------------------------------------------------------*/
void sm_append_adv_field(uint8_t *buffer, uint16_t max_len, uint16_t *offset,
    uint8_t type, const uint8_t *value, uint8_t value_len);
bool sm_parse_adv_data(const uint8_t *data, uint16_t data_len,
    const uint8_t **mesh_payload, uint16_t *mesh_len);
void sm_build_adv_data_direct(uint8_t *adv_buf, uint16_t *adv_len, const uint8_t *frame, uint16_t frame_len);
void sm_build_scan_rsp(uint8_t *buf, uint16_t *len);
void sm_refresh_advertising(const uint8_t *frame, uint16_t frame_len);
void sm_set_announce_params(void);
void sm_start_scan(void);
void sm_stop_scan(void);

/*--------------------------------------------------------------------------
 * Module: SM_mesh_scan.c / 扫描与邻居管理
 *--------------------------------------------------------------------------*/
void sm_update_neighbor(uint8_t addr, int8_t rssi);
void sm_age_neighbors(void);
uint8_t sm_get_neighbor_report(uint8_t *entries, uint16_t max_len);
void sm_handle_seek_result(sle_seek_result_info_t *result);

/*--------------------------------------------------------------------------
 * Module: SM_mesh_node.c / 普通节点逻辑
 *--------------------------------------------------------------------------*/
void sm_node_handle_frame(const sm_frame_view_t *frame, int8_t rssi);
void sm_node_send_rssi_report(void);
void sm_node_send_path_req(uint8_t dst_addr);
void sm_node_send_data(uint8_t dst_addr, const uint8_t *payload, uint16_t payload_len);
void sm_node_send_ack(uint8_t dst_addr, uint16_t seq);
void sm_relay_frame(const uint8_t *frame, uint16_t frame_len);
bool sm_dedup_check(uint8_t frame_type, uint8_t src_addr, uint16_t seq);
void sm_dedup_add(uint8_t frame_type, uint8_t src_addr, uint16_t seq);
void sm_store_path(uint8_t dst_addr, const uint8_t *path, uint8_t path_len);

/*--------------------------------------------------------------------------
 * Module: SM_mesh_dongle.c / dongle 逻辑
 *--------------------------------------------------------------------------*/
void sm_dongle_handle_frame(const sm_frame_view_t *frame, int8_t rssi);
void sm_dongle_send_path_resp(uint8_t src_addr, uint8_t dst_addr,
    const uint8_t *path, uint8_t path_len);
void sm_dongle_forward_to_pc(const char *fmt, ...);
void sm_dongle_flush_rssi(void);
void sm_dongle_role_tick(void);
void sm_dongle_print_rssi_store(void);

/*--------------------------------------------------------------------------
 * Module: SM_mesh_uart.c / UART 接口
 *--------------------------------------------------------------------------*/
void sm_uart_register_rx(void);
void sm_uart_handle_input(void);
void sm_uart_printf(const char *fmt, ...);

/*--------------------------------------------------------------------------
 * Module: SM_mesh.c / 主入口
 *--------------------------------------------------------------------------*/
uint64_t sm_now_ms(void);

#endif
