#include "mock_data_generator.h"
#include "modbus_sim.h"
#include "notify_printer.h"
#include "ipc_sender.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* SLE 帧格式常量 */
#define SLE_FRAME_MAGIC_0       0x53  /* 'S' */
#define SLE_FRAME_MAGIC_1       0x54  /* 'T' */
#define SLE_FRAME_VERSION       0x01
#define SLE_FRAME_TYPE_HEARTBEAT 1
#define SLE_FRAME_TYPE_DATA     2
#define SLE_ROLE_ROOT           1
#define SLE_ROLE_RELAY          2
#define SLE_ROLE_LEAF           3
#define SLE_FRAME_HEADER_LEN    13
#define SLE_FRAME_MAX_LEN       256

/* 模拟设备配置 */
typedef struct {
    int server_index;
    int modbus_type;
    const char *name;
} mock_device_t;

static const mock_device_t g_mock_devices[] = {
    {0, MODBUS_TYPE_METER, "METER_001"},
    {1, MODBUS_TYPE_METER, "METER_002"},
    {2, MODBUS_TYPE_METER, "METER_003"},
    {3, MODBUS_TYPE_METER, "METER_004"},
    {4, MODBUS_TYPE_METER, "METER_005"},
    {5, MODBUS_TYPE_METER, "METER_006"},
    {6, MODBUS_TYPE_METER, "METER_007"},
    {7, MODBUS_TYPE_ENV, "ENV_001"},
    {8, MODBUS_TYPE_ENV, "ENV_002"},
    {9, MODBUS_TYPE_RELAY, "RELAY_001"},
    {10, MODBUS_TYPE_RELAY, "RELAY_002"},
};

#define MOCK_DEVICE_COUNT (sizeof(g_mock_devices) / sizeof(g_mock_devices[0]))

/* DTU 节点配置 */
typedef struct {
    int node_id;
    int parent_id;  /* 0 = root */
    const char *name;
} mock_dtu_node_t;

/* 两棵树：DTU_001 (root, 外接设备树) 和 DTU_012 (root, SLE 中继树) */
static const mock_dtu_node_t g_dtu_nodes[] = {
    /* 树1：外接设备树 (DTU_001 为 root) */
    {1,  0,  "DTU_001"},   /* root */
    {2,  1,  "DTU_002"},   /* child of 1 */
    {3,  1,  "DTU_003"},   /* child of 1 */
    {4,  2,  "DTU_004"},   /* child of 2 */
    {5,  2,  "DTU_005"},   /* child of 2 */
    {6,  3,  "DTU_006"},   /* child of 3 */
    {7,  3,  "DTU_007"},   /* child of 3 */
    {8,  1,  "DTU_008"},   /* child of 1 */
    {9,  1,  "DTU_009"},   /* child of 1 */
    {10, 1,  "DTU_010"},   /* child of 1 */
    {11, 1,  "DTU_011"},   /* child of 1 */
    /* 树2：SLE 中继树 (DTU_012 为 root) */
    {12, 0,  "DTU_012"},   /* root */
    {13, 12, "DTU_013"},   /* child of 12 */
    {14, 12, "DTU_014"},   /* child of 12 */
    {15, 12, "DTU_015"},   /* child of 12 */
    {16, 13, "DTU_016"},   /* child of 13 */
    {17, 13, "DTU_017"},   /* child of 13 */
    {18, 13, "DTU_018"},   /* child of 13 */
    {19, 14, "DTU_019"},   /* child of 14 */
    {20, 14, "DTU_020"},   /* child of 14 */
    {21, 15, "DTU_021"},   /* child of 15 */
    {22, 15, "DTU_022"},   /* child of 15 */
    {23, 12, "DTU_023"},   /* child of 12 */
    {24, 23, "DTU_024"},   /* child of 23 */
    {25, 23, "DTU_025"},   /* child of 23 */
    {26, 12, "DTU_026"},   /* child of 12 */
    {27, 26, "DTU_027"},   /* child of 26 */
    {28, 26, "DTU_028"},   /* child of 26 */
    {29, 12, "DTU_029"},   /* child of 12 */
    {30, 29, "DTU_030"},   /* child of 29 */
    {31, 29, "DTU_031"},   /* child of 29 */
};

#define DTU_NODE_COUNT (sizeof(g_dtu_nodes) / sizeof(g_dtu_nodes[0]))

static pthread_t g_thread;
static atomic_bool g_running = ATOMIC_VAR_INIT(false);

/* 构建 SLE 帧 */
static uint16_t build_sle_frame(uint8_t *out, uint16_t out_size,
                                uint16_t src_node_id, uint8_t modbus_type,
                                const uint8_t *modbus_data, uint16_t modbus_len)
{
    uint16_t payload_len = 2 + modbus_len; /* modbus_type(1) + modbus_len(1) + modbus_data */
    uint16_t frame_len = SLE_FRAME_HEADER_LEN + payload_len;

    if (frame_len > out_size || frame_len > SLE_FRAME_MAX_LEN) {
        return 0;
    }

    /* 帧头 */
    out[0] = SLE_FRAME_MAGIC_0;
    out[1] = SLE_FRAME_MAGIC_1;
    out[2] = SLE_FRAME_VERSION;
    out[3] = SLE_FRAME_TYPE_DATA;
    out[4] = SLE_ROLE_LEAF;
    /* src_node_id (小端) */
    out[5] = src_node_id & 0xFF;
    out[6] = (src_node_id >> 8) & 0xFF;
    /* dst_node_id = 0 (网关) */
    out[7] = 0;
    out[8] = 0;
    /* seq = 0 */
    out[9] = 0;
    out[10] = 0;
    /* payload_len (小端) */
    out[11] = payload_len & 0xFF;
    out[12] = (payload_len >> 8) & 0xFF;

    /* payload: modbus_type + modbus_len + modbus_data */
    out[13] = modbus_type;
    out[14] = (uint8_t)modbus_len;
    memcpy(out + 15, modbus_data, modbus_len);

    return frame_len;
}

/* 构建心跳帧 */
static uint16_t build_heartbeat_frame(uint8_t *out, uint16_t out_size,
                                      uint16_t node_id, uint8_t role)
{
    /* 心跳帧: magic(2) + version(1) + type(1) + role(1) + src_node_id(2) + dst_node_id(2) + seq(2) + payload_len(2) + payload */
    uint16_t frame_len = SLE_FRAME_HEADER_LEN + 1; /* payload = 1 byte (role) */

    if (frame_len > out_size || frame_len > SLE_FRAME_MAX_LEN) {
        return 0;
    }

    /* 帧头 */
    out[0] = SLE_FRAME_MAGIC_0;
    out[1] = SLE_FRAME_MAGIC_1;
    out[2] = SLE_FRAME_VERSION;
    out[3] = SLE_FRAME_TYPE_HEARTBEAT;
    out[4] = role;
    /* src_node_id (小端) */
    out[5] = node_id & 0xFF;
    out[6] = (node_id >> 8) & 0xFF;
    /* dst_node_id = 0 (网关) */
    out[7] = 0;
    out[8] = 0;
    /* seq = 0 */
    out[9] = 0;
    out[10] = 0;
    /* payload_len = 1 (小端) */
    out[11] = 1;
    out[12] = 0;

    /* payload: role */
    out[13] = role;

    return frame_len;
}

/* 预分配的帧缓冲区 */
static uint8_t g_frame_bufs[MOCK_DEVICE_COUNT + DTU_NODE_COUNT][SLE_FRAME_MAX_LEN];

static void *mock_thread_func(void *arg)
{
    (void)arg;
    uint64_t tick = 0;

    fprintf(stderr, "[MOCK] mock data generator started, %zu devices, %zu DTU nodes\n",
            MOCK_DEVICE_COUNT, DTU_NODE_COUNT);

    struct timespec sleep_time = {5, 0};  /* 5 秒 */

    while (atomic_load(&g_running)) {
        tick++;

        uint8_t modbus_buf[MODBUS_FRAME_MAX_LEN];
        uint16_t modbus_len;
        int frame_count = 0;

        /* 1. 生成外部设备数据帧 */
        for (size_t i = 0; i < MOCK_DEVICE_COUNT; i++) {
            if (modbus_sim_generate(tick, g_mock_devices[i].server_index,
                                    g_mock_devices[i].modbus_type, modbus_buf, &modbus_len)) {
                /* 构建 SLE 帧 */
                uint16_t src_node_id = (uint16_t)(g_mock_devices[i].server_index + 1);
                uint16_t sle_len = build_sle_frame(g_frame_bufs[frame_count],
                                                   SLE_FRAME_MAX_LEN,
                                                   src_node_id, g_mock_devices[i].modbus_type,
                                                   modbus_buf, modbus_len);
                if (sle_len > 0) {
                    frame_count++;

                    /* 创建虚拟连接结构用于入队 */
                    sle_server_connection_t mock_conn;
                    memset(&mock_conn, 0, sizeof(mock_conn));
                    mock_conn.conn_id = (uint16_t)(0xF000 + g_mock_devices[i].server_index);
                    mock_conn.rx_count = (uint32_t)tick;

                    notify_printer_enqueue_packet(g_mock_devices[i].server_index,
                                                  &mock_conn, g_frame_bufs[frame_count - 1], sle_len);
                }
            }
        }

        /* 2. 生成 DTU 心跳帧 */
        for (size_t i = 0; i < DTU_NODE_COUNT; i++) {
            uint8_t role = (g_dtu_nodes[i].parent_id == 0) ? SLE_ROLE_ROOT : SLE_ROLE_LEAF;
            uint16_t sle_len = build_heartbeat_frame(g_frame_bufs[frame_count],
                                                     SLE_FRAME_MAX_LEN,
                                                     (uint16_t)g_dtu_nodes[i].node_id, role);
            if (sle_len > 0) {
                frame_count++;

                sle_server_connection_t mock_conn;
                memset(&mock_conn, 0, sizeof(mock_conn));
                mock_conn.conn_id = (uint16_t)(0xF100 + g_dtu_nodes[i].node_id);
                mock_conn.rx_count = (uint32_t)tick;

                /* 使用 node_id 作为 server_index */
                notify_printer_enqueue_packet(g_dtu_nodes[i].node_id,
                                              &mock_conn, g_frame_bufs[frame_count - 1], sle_len);
            }
        }

        fprintf(stderr, "[MOCK] generated %zu device + %zu DTU frames, tick=%llu\n",
                MOCK_DEVICE_COUNT, DTU_NODE_COUNT, (unsigned long long)tick);

        /* 使用 nanosleep 替代 sleep，支持更精确的中断 */
        nanosleep(&sleep_time, NULL);
    }

    fprintf(stderr, "[MOCK] mock data generator stopped\n");
    return NULL;
}

int mock_data_generator_init(void)
{
    modbus_sim_init((int)MOCK_DEVICE_COUNT);
    return 0;
}

int mock_data_generator_start(void)
{
    if (atomic_load(&g_running)) {
        return 0;
    }

    atomic_store(&g_running, true);
    if (pthread_create(&g_thread, NULL, mock_thread_func, NULL) != 0) {
        atomic_store(&g_running, false);
        fprintf(stderr, "[MOCK] failed to create mock thread\n");
        return -1;
    }

    return 0;
}

void mock_data_generator_stop(void)
{
    if (!atomic_load(&g_running)) {
        return;
    }

    atomic_store(&g_running, false);
    pthread_join(g_thread, NULL);
}
