#ifndef GATEWAY_SLE_SERVER_CONNECTIONS_H
#define GATEWAY_SLE_SERVER_CONNECTIONS_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "sle_common.h"

/* ==========================================================================
 * 连接表常量定义
 * ========================================================================== */
#define SLE_MAX_SERVER_CONNECTIONS      8
#define SLE_INVALID_CONN_ID             0xFFFF
#define SLE_ADDR_LEN                    6
#define SLE_ADDR_STR_LEN                18      /* "xx:xx:xx:xx:xx:xx" + '\0' */
#define SLE_INVALID_SERVER_INDEX        (-1)

/* 连接状态超时默认值（毫秒） */
#define SLE_DEFAULT_CONNECTING_TIMEOUT_MS   10000
#define SLE_DEFAULT_PAIRING_TIMEOUT_MS      8000
#define SLE_DEFAULT_DISCOVERY_TIMEOUT_MS    8000
#define SLE_DEFAULT_PARAM_UPDATE_TIMEOUT_MS 5000

/* 单个 SLE server 的生命周期状态。 */
typedef enum {
    SLE_SERVER_IDLE = 0,
    SLE_SERVER_CONNECTING,
    SLE_SERVER_CONNECTED,
    SLE_SERVER_PAIRING,
    SLE_SERVER_DISCOVERING,
    SLE_SERVER_READY,
    SLE_SERVER_DISCONNECTED,
} sle_server_connection_state_t;

/**
 * @brief 单个 SLE server 连接槽位
 * @note 地址是稳定身份，conn_id 是每次连接后 SDK 分配的运行期句柄
 */
typedef struct {
    /* 基本状态 */
    bool used;                              /* 槽位是否已分配 */
    sle_server_connection_state_t state;    /* 当前连接状态 */
    sle_addr_t addr;                        /* 对端设备地址 */
    uint16_t conn_id;                       /* SDK分配的连接ID */
    uint8_t pair_state;                     /* 配对状态 */
    
    /* 连接流程标志 */
    bool mtu_done;                          /* MTU交换是否完成 */
    bool discovery_done;                    /* 服务发现是否完成 */
    bool param_update_done;                 /* 连接参数更新是否完成 */
    bool auth_complete_received;            /* 是否收到auth完成回调 */
    bool keep_pair_info;                    /* 断连后保留配对信息用于快速重连 */
    
    /* 属性句柄 */
    uint16_t notify_handle;                 /* notify属性句柄 */
    uint16_t write_handle;                  /* write属性句柄 */
    uint16_t service_start_handle;          /* 服务起始句柄（保留用于重连） */
    uint16_t service_end_handle;            /* 服务结束句柄（保留用于重连） */
    
    /* 时间戳 */
    uint64_t last_state_ms;                 /* 最后状态变更时间 */
    uint64_t last_rx_ms;                    /* 最后接收数据时间 */
    uint64_t state_enter_ms;                /* 进入当前状态的时间 */
    
    /* 数据统计 */
    uint32_t rx_count;                      /* 接收数据包计数 */
    
    /* 超时配置 */
    uint32_t state_timeout_ms;              /* 当前状态的超时时间 */
    
    /* 断开原因 */
    uint32_t disconnect_reason;             /* 断开连接的原因码 */
} sle_server_connection_t;

/* 一对多连接表。所有回调都必须先通过这里把 addr/conn_id 映射到 server_index。 */
typedef struct {
    pthread_mutex_t mutex;
    sle_server_connection_t servers[SLE_MAX_SERVER_CONNECTIONS];
    uint8_t max_connections;
} sle_server_connections_t;

/* 初始化连接表，max_connections 最大不超过 SLE_MAX_SERVER_CONNECTIONS。 */
void server_connections_init(sle_server_connections_t *table, uint8_t max_connections);

/* 销毁连接表内部锁。 */
void server_connections_deinit(sle_server_connections_t *table);

/* 按 server 地址查 server_index；用于扫描结果去重和重连复用。 */
int server_connections_find_by_addr(sle_server_connections_t *table, const sle_addr_t *addr);

/* 按 SDK conn_id 查 server_index；用于 MTU、发现、notify 等连接事件。 */
int server_connections_find_by_conn_id(sle_server_connections_t *table, uint16_t conn_id);

/* 按地址复用旧 server_index，或为新 server 分配空位。 */
int server_connections_alloc_or_reuse(sle_server_connections_t *table, const sle_addr_t *addr, bool *is_new);

/* 判断是否还能接入新 server；断线 server 的位置可被同地址重连复用。 */
bool server_connections_has_capacity(sle_server_connections_t *table);

/* 标记已发起连接请求，等待 SDK 返回 connected 事件。 */
void server_connections_mark_connecting(sle_server_connections_t *table, int server_index, const sle_addr_t *addr);

/* 标记 SDK 已连接，并写入本次连接的 conn_id。 */
void server_connections_mark_connected(sle_server_connections_t *table, int server_index, uint16_t conn_id, uint8_t pair_state);

/* 标记正在配对。 */
void server_connections_mark_pairing(sle_server_connections_t *table, int server_index);

/* 标记正在执行 MTU 后的服务/属性发现。 */
void server_connections_mark_discovering(sle_server_connections_t *table, int server_index);

/* 标记该 server 已完成发现，可以接收 notify。 */
void server_connections_mark_ready(sle_server_connections_t *table, int server_index);

/* 标记断线，并清空本次连接相关的 conn_id/handle/发现状态。 */
void server_connections_mark_disconnected(sle_server_connections_t *table, int server_index, uint32_t reason);

/* 更新最近一次连接事件时间，用于应用层保活判断。 */
void server_connections_touch_state(sle_server_connections_t *table, int server_index, uint64_t now_ms);

/* 记录该连接是否完成 MTU exchange。 */
void server_connections_set_mtu_done(sle_server_connections_t *table, int server_index, bool done);

/* 缓存 notify 属性 handle；重连后必须重新发现并覆盖。 */
void server_connections_set_notify_handle(sle_server_connections_t *table, int server_index, uint16_t handle);

/* 缓存写属性 handle；后续做下行控制时使用。 */
void server_connections_set_write_handle(sle_server_connections_t *table, int server_index, uint16_t handle);

/* 记录一次上行数据，用于观察每个 server 的收包情况。 */
void server_connections_record_rx(sle_server_connections_t *table, int server_index, uint64_t now_ms);

/* 拷贝一个 server 连接快照，避免打印时长时间持有连接表锁。 */
bool server_connections_get_server_copy(sle_server_connections_t *table, int server_index, sle_server_connection_t *out);

/* 返回当前已有 SDK conn_id 的 server 数量；connecting 不计入真实连接数。 */
int server_connections_active_count(sle_server_connections_t *table);

/* 将连接状态转换为日志可读字符串。 */
const char *server_connections_state_name(sle_server_connection_state_t state);

/* 将 SLE 地址格式化为 xx:xx:xx:xx:xx:xx。 */
void server_connections_addr_to_string(const sle_addr_t *addr, char *buf, uint32_t buf_len);

/* ==========================================================================
 * 超时管理
 * ========================================================================== */

/**
 * @brief 设置连接状态超时时间
 * @param table 连接表指针
 * @param server_index 服务器索引
 * @param timeout_ms 超时时间（毫秒），0表示不超时
 */
void server_connections_set_state_timeout(sle_server_connections_t *table, 
    int server_index, uint32_t timeout_ms);

/**
 * @brief 检查连接状态是否已超时
 * @param table 连接表指针
 * @param server_index 服务器索引
 * @param now_ms 当前时间（毫秒）
 * @return true表示已超时
 */
bool server_connections_is_state_timeout(sle_server_connections_t *table, 
    int server_index, uint64_t now_ms);

/**
 * @brief 设置连接参数更新完成标志
 * @param table 连接表指针
 * @param server_index 服务器索引
 * @param done 是否完成
 */
void server_connections_set_param_update_done(sle_server_connections_t *table, 
    int server_index, bool done);

/**
 * @brief 设置auth完成标志
 * @param table 连接表指针
 * @param server_index 服务器索引
 * @param received 是否已收到
 */
void server_connections_set_auth_complete(sle_server_connections_t *table, 
    int server_index, bool received);

/* ==========================================================================
 * 调试辅助
 * ========================================================================== */

/**
 * @brief 打印连接表当前状态（调试用）
 * @param table 连接表指针
 * @param tag 日志标签
 */
void server_connections_dump_table(sle_server_connections_t *table, const char *tag);

/**
 * @brief 获取连接表统计信息
 * @param table 连接表指针
 * @param out_total 输出：已使用的槽位数
 * @param out_active 输出：活跃连接数（非idle/disconnected）
 * @param out_ready 输出：就绪连接数
 */
void server_connections_get_stats(sle_server_connections_t *table,
    uint8_t *out_total, uint8_t *out_active, uint8_t *out_ready);

/**
 * @brief 验证状态转换是否合法
 * @param from 源状态
 * @param to 目标状态
 * @return true表示转换合法
 */
bool server_connections_is_valid_transition(sle_server_connection_state_t from, 
    sle_server_connection_state_t to);

/* ==========================================================================
 * 快速重连支持
 * ========================================================================== */

/**
 * @brief 设置keep_pair_info标志
 * @param table 连接表指针
 * @param server_index 服务器索引
 * @param keep 是否保留配对信息
 */
void server_connections_set_keep_pair_info(sle_server_connections_t *table, 
    int server_index, bool keep);

/**
 * @brief 设置服务句柄（保留用于快速重连）
 * @param table 连接表指针
 * @param server_index 服务器索引
 * @param start_handle 服务起始句柄
 * @param end_handle 服务结束句柄
 */
void server_connections_set_service_handles(sle_server_connections_t *table, 
    int server_index, uint16_t start_handle, uint16_t end_handle);

/**
 * @brief 检查是否有保留的配对信息
 * @param table 连接表指针
 * @param addr 设备地址
 * @return server_index，-1表示未找到
 */
int server_connections_find_reconnectable(sle_server_connections_t *table, const sle_addr_t *addr);

#endif
