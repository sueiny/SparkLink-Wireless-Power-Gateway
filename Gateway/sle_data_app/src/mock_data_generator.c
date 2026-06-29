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
#define SLE_FRAME_TYPE_DATA     2
#define SLE_FRAME_TYPE_DTU_TOPOLOGY 5
#define SLE_FRAME_TYPE_EXTERNAL_MAP 6
#define SLE_ROLE_ROOT           1
#define SLE_ROLE_RELAY          2
#define SLE_ROLE_LEAF           3
#define SLE_FRAME_HEADER_LEN    13
#define SLE_FRAME_MAX_LEN       1024

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
    {8, MODBUS_TYPE_RELAY, "RELAY_001"},
};

#define MOCK_DEVICE_COUNT (sizeof(g_mock_devices) / sizeof(g_mock_devices[0]))

static pthread_t g_thread;
static atomic_bool g_running = ATOMIC_VAR_INIT(false);

/* 构建 SLE 帧 */
static uint16_t build_sle_frame(uint8_t *out, uint16_t out_size,
                                uint16_t src_node_id,
                                const uint8_t *modbus_data, uint16_t modbus_len)
{
    uint16_t payload_len = modbus_len; /* DATA payload is pure Modbus RTU */
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

    /* payload: pure Modbus RTU */
    memcpy(out + SLE_FRAME_HEADER_LEN, modbus_data, modbus_len);

    return frame_len;
}

static uint16_t build_control_frame(uint8_t *out, uint16_t out_size,
                                    uint8_t frame_type, uint16_t src_node_id,
                                    const uint8_t *payload, uint16_t payload_len)
{
    uint16_t frame_len = SLE_FRAME_HEADER_LEN + payload_len;

    if (frame_len > out_size || frame_len > SLE_FRAME_MAX_LEN) {
        return 0;
    }

    /* 帧头 */
    out[0] = SLE_FRAME_MAGIC_0;
    out[1] = SLE_FRAME_MAGIC_1;
    out[2] = SLE_FRAME_VERSION;
    out[3] = frame_type;
    out[4] = SLE_ROLE_ROOT;
    /* src_node_id (小端) */
    out[5] = src_node_id & 0xFF;
    out[6] = (src_node_id >> 8) & 0xFF;
    /* dst_node_id = 0 (网关) */
    out[7] = 0;
    out[8] = 0;
    /* seq = 0 */
    out[9] = 0;
    out[10] = 0;
    out[11] = payload_len & 0xFF;
    out[12] = (payload_len >> 8) & 0xFF;

    if (payload_len > 0 && payload != NULL)
        memcpy(out + SLE_FRAME_HEADER_LEN, payload, payload_len);

    return frame_len;
}

/* 预分配的帧缓冲区 */
static uint8_t g_frame_bufs[MOCK_DEVICE_COUNT + 3][SLE_FRAME_MAX_LEN];

static uint16_t build_topology_text(uint16_t root_id, char *out, size_t out_size)
{
    int written = 0;
    if (root_id == 1) {
        written = snprintf(out, out_size,
                           "1\n"
                           "|- 2\n"
                           "|- 3\n"
                           "|- 4\n"
                           "|- 5\n"
                           "|- 6\n"
                           "|- 7\n"
                           "|- 8\n"
                           "`- 9\n");
    } else {
        written = snprintf(out, out_size, "10\n");
        for (int node_id = 11; node_id <= 69 && written > 0 &&
             (size_t)written < out_size; ++node_id) {
            written += snprintf(out + written, out_size - (size_t)written,
                                "%s %d\n", node_id == 69 ? "`-" : "|-", node_id);
        }
    }
    if (written <= 0)
        return 0;
    if ((size_t)written >= out_size)
        return 0;
    return (uint16_t)written;
}

static uint16_t build_external_map_text(char *out, size_t out_size)
{
    int written = snprintf(out, out_size,
                           "DTU_001-METER_001\n"
                           "DTU_002-METER_002\n"
                           "DTU_003-METER_003\n"
                           "DTU_004-METER_004\n"
                           "DTU_005-METER_005\n"
                           "DTU_006-METER_006\n"
                           "DTU_007-METER_007\n"
                           "DTU_008-ENV_001\n"
                           "DTU_009-RELAY_001\n");
    if (written <= 0 || (size_t)written >= out_size)
        return 0;
    return (uint16_t)written;
}

static void enqueue_mock_frame(int server_index, uint16_t conn_id, uint64_t tick,
                               const uint8_t *frame, uint16_t frame_len)
{
    sle_server_connection_t mock_conn;
    memset(&mock_conn, 0, sizeof(mock_conn));
    mock_conn.conn_id = conn_id;
    mock_conn.rx_count = (uint32_t)tick;
    notify_printer_enqueue_packet(server_index, &mock_conn, frame, frame_len);
}

static void *mock_thread_func(void *arg)
{
    (void)arg;
    uint64_t tick = 0;

    fprintf(stderr, "[MOCK] mock data generator started, %zu devices, root_report topology\n",
            MOCK_DEVICE_COUNT);

    struct timespec sleep_time = {5, 0};  /* 5 秒 */

    while (atomic_load(&g_running)) {
        tick++;

        uint8_t modbus_buf[MODBUS_FRAME_MAX_LEN];
        uint16_t modbus_len;
        int frame_count = 0;

        /* 1. 生成 root_report 05/06 快照，正式模式只接受 DATA/05/06。 */
        char text_buf[768];
        uint16_t text_len = build_topology_text(1, text_buf, sizeof(text_buf));
        if (text_len > 0) {
            uint16_t sle_len = build_control_frame(g_frame_bufs[frame_count],
                                                   SLE_FRAME_MAX_LEN,
                                                   SLE_FRAME_TYPE_DTU_TOPOLOGY,
                                                   1,
                                                   (const uint8_t *)text_buf,
                                                   text_len);
            if (sle_len > 0)
                enqueue_mock_frame(1, 0xF101, tick, g_frame_bufs[frame_count++], sle_len);
        }

        text_len = build_topology_text(10, text_buf, sizeof(text_buf));
        if (text_len > 0) {
            uint16_t sle_len = build_control_frame(g_frame_bufs[frame_count],
                                                   SLE_FRAME_MAX_LEN,
                                                   SLE_FRAME_TYPE_DTU_TOPOLOGY,
                                                   10,
                                                   (const uint8_t *)text_buf,
                                                   text_len);
            if (sle_len > 0)
                enqueue_mock_frame(10, 0xF10A, tick, g_frame_bufs[frame_count++], sle_len);
        }

        text_len = build_external_map_text(text_buf, sizeof(text_buf));
        if (text_len > 0) {
            uint16_t sle_len = build_control_frame(g_frame_bufs[frame_count],
                                                   SLE_FRAME_MAX_LEN,
                                                   SLE_FRAME_TYPE_EXTERNAL_MAP,
                                                   1,
                                                   (const uint8_t *)text_buf,
                                                   text_len);
            if (sle_len > 0)
                enqueue_mock_frame(1, 0xF201, tick, g_frame_bufs[frame_count++], sle_len);
        }

        /* 2. 生成外部设备 DATA，payload 为纯 Modbus RTU。 */
        for (size_t i = 0; i < MOCK_DEVICE_COUNT; i++) {
            if (modbus_sim_generate(tick, g_mock_devices[i].server_index,
                                    g_mock_devices[i].modbus_type, modbus_buf, &modbus_len)) {
                /* 构建 SLE 帧 */
                uint16_t src_node_id = (uint16_t)(g_mock_devices[i].server_index + 1);
                uint16_t sle_len = build_sle_frame(g_frame_bufs[frame_count],
                                                   SLE_FRAME_MAX_LEN,
                                                   src_node_id,
                                                   modbus_buf, modbus_len);
                if (sle_len > 0) {
                    enqueue_mock_frame(g_mock_devices[i].server_index,
                                       (uint16_t)(0xF000 + g_mock_devices[i].server_index),
                                       tick,
                                       g_frame_bufs[frame_count],
                                       sle_len);
                    frame_count++;
                }
            }
        }

        fprintf(stderr, "[MOCK] generated root_report snapshots + %zu DATA frames, tick=%llu\n",
                MOCK_DEVICE_COUNT, (unsigned long long)tick);

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
