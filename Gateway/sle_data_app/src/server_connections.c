#include "server_connections.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static uint64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static bool addr_equal(const sle_addr_t *a, const sle_addr_t *b)
{
    return a != NULL && b != NULL && a->type == b->type && memcmp(a->addr, b->addr, SLE_ADDR_LEN) == 0;
}

/* 初始化固定长度连接表。server_index 是本 APP 内部稳定编号，不等同于 SDK conn_id。 */
void server_connections_init(sle_server_connections_t *table, uint8_t max_connections)
{
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(*table));
    pthread_mutex_init(&table->mutex, NULL);
    table->max_connections = max_connections;
    if (table->max_connections == 0 || table->max_connections > SLE_MAX_SERVER_CONNECTIONS) {
        table->max_connections = SLE_MAX_SERVER_CONNECTIONS;
    }
    for (uint8_t i = 0; i < SLE_MAX_SERVER_CONNECTIONS; ++i) {
        table->servers[i].state = SLE_SERVER_IDLE;
        table->servers[i].conn_id = SLE_INVALID_CONN_ID;
    }
}

void server_connections_deinit(sle_server_connections_t *table)
{
    if (table == NULL) {
        return;
    }
    pthread_mutex_destroy(&table->mutex);
}

/* 扫描阶段用地址去重：同一 server 反复广播时不能重复分配位置。 */
int server_connections_find_by_addr(sle_server_connections_t *table, const sle_addr_t *addr)
{
    if (table == NULL || addr == NULL) {
        return -1;
    }
    pthread_mutex_lock(&table->mutex);
    int found = -1;
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        if (table->servers[i].used && addr_equal(&table->servers[i].addr, addr)) {
            found = (int)i;
            break;
        }
    }
    pthread_mutex_unlock(&table->mutex);
    return found;
}

/* 连接后所有 SDK 事件只带 conn_id，因此必须通过 conn_id 找回 server_index。 */
int server_connections_find_by_conn_id(sle_server_connections_t *table, uint16_t conn_id)
{
    if (table == NULL) {
        return -1;
    }
    pthread_mutex_lock(&table->mutex);
    int found = -1;
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        if (table->servers[i].used && table->servers[i].conn_id == conn_id) {
            found = (int)i;
            break;
        }
    }
    pthread_mutex_unlock(&table->mutex);
    return found;
}

/* 地址是 server 的稳定身份；断线重连时复用旧位置，但更新新的 conn_id。 */
int server_connections_alloc_or_reuse(sle_server_connections_t *table, const sle_addr_t *addr, bool *is_new)
{
    if (table == NULL || addr == NULL) {
        return -1;
    }
    pthread_mutex_lock(&table->mutex);
    if (is_new != NULL) {
        *is_new = false;
    }
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        if (table->servers[i].used && addr_equal(&table->servers[i].addr, addr)) {
            pthread_mutex_unlock(&table->mutex);
            return (int)i;
        }
    }
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        if (!table->servers[i].used || table->servers[i].state == SLE_SERVER_DISCONNECTED) {
            memset(&table->servers[i], 0, sizeof(table->servers[i]));
            table->servers[i].used = true;
            table->servers[i].state = SLE_SERVER_IDLE;
            table->servers[i].addr = *addr;
            table->servers[i].conn_id = SLE_INVALID_CONN_ID;
            if (is_new != NULL) {
                *is_new = true;
            }
            pthread_mutex_unlock(&table->mutex);
            return (int)i;
        }
    }
    pthread_mutex_unlock(&table->mutex);
    return -1;
}

bool server_connections_has_capacity(sle_server_connections_t *table)
{
    if (table == NULL) {
        return false;
    }
    pthread_mutex_lock(&table->mutex);
    bool has_capacity = false;
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        if (!table->servers[i].used ||
            (table->servers[i].used && table->servers[i].state == SLE_SERVER_DISCONNECTED)) {
            has_capacity = true;
            break;
        }
    }
    pthread_mutex_unlock(&table->mutex);
    return has_capacity;
}

/**
 * @brief 标记已发起连接请求，等待 SDK 返回 connected 事件
 * @note 发起连接前先清空运行期状态，避免重连时误用旧 handle
 */
void server_connections_mark_connecting(sle_server_connections_t *table, int server_index, const sle_addr_t *addr)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].used = true;
    if (addr != NULL) {
        table->servers[server_index].addr = *addr;
    }
    table->servers[server_index].state = SLE_SERVER_CONNECTING;
    table->servers[server_index].conn_id = SLE_INVALID_CONN_ID;
    
    /* 重置连接流程标志 */
    table->servers[server_index].mtu_done = false;
    table->servers[server_index].discovery_done = false;
    table->servers[server_index].param_update_done = false;
    table->servers[server_index].auth_complete_received = false;
    
    /* 重置属性句柄 */
    table->servers[server_index].notify_handle = 0;
    table->servers[server_index].write_handle = 0;
    
    /* 重置时间戳 */
    table->servers[server_index].last_state_ms = 0;
    table->servers[server_index].last_rx_ms = 0;
    table->servers[server_index].state_enter_ms = now_ms();
    table->servers[server_index].rx_count = 0;
    
    /* 超时由外部设置 */
    table->servers[server_index].state_timeout_ms = 0;
    
    /* 重置断开原因 */
    table->servers[server_index].disconnect_reason = 0;
    
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_mark_connected(sle_server_connections_t *table, int server_index, uint16_t conn_id, uint8_t pair_state)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].state = SLE_SERVER_CONNECTED;
    table->servers[server_index].conn_id = conn_id;
    table->servers[server_index].pair_state = pair_state;
    
    /* 重置连接流程标志 */
    table->servers[server_index].mtu_done = false;
    table->servers[server_index].discovery_done = false;
    table->servers[server_index].param_update_done = false;
    table->servers[server_index].auth_complete_received = false;
    
    /* 重置属性句柄 */
    table->servers[server_index].notify_handle = 0;
    table->servers[server_index].write_handle = 0;
    
    /* 更新时间戳 */
    table->servers[server_index].last_state_ms = now_ms();
    table->servers[server_index].state_enter_ms = now_ms();
    
    /* 超时由外部设置 */
    table->servers[server_index].state_timeout_ms = 0;
    
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_mark_pairing(sle_server_connections_t *table, int server_index)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].state = SLE_SERVER_PAIRING;
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_mark_discovering(sle_server_connections_t *table, int server_index)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].state = SLE_SERVER_DISCOVERING;
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_mark_ready(sle_server_connections_t *table, int server_index)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].state = SLE_SERVER_READY;
    table->servers[server_index].discovery_done = true;
    pthread_mutex_unlock(&table->mutex);
}

/**
 * @brief 标记断开连接，并清空本次连接相关的 conn_id/handle/发现状态
 * @note 断线后保留地址，这样同地址重连能复用 server_index
 * @note 如果discovery_done为true，保留配对信息用于快速重连
 */
void server_connections_mark_disconnected(sle_server_connections_t *table, int server_index, uint32_t reason)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    
    /* 检查是否需要保留配对信息用于快速重连 */
    bool was_ready = (table->servers[server_index].state == SLE_SERVER_READY);
    bool had_discovery = table->servers[server_index].discovery_done;
    
    table->servers[server_index].state = SLE_SERVER_DISCONNECTED;
    table->servers[server_index].conn_id = SLE_INVALID_CONN_ID;
    
    /* 如果之前已完成discovery，保留配对信息 */
    if (was_ready || had_discovery) {
        table->servers[server_index].keep_pair_info = true;
        /* 保留notify_handle和write_handle */
        /* 保留service_start_handle和service_end_handle */
    } else {
        table->servers[server_index].keep_pair_info = false;
        table->servers[server_index].notify_handle = 0;
        table->servers[server_index].write_handle = 0;
        table->servers[server_index].service_start_handle = 0;
        table->servers[server_index].service_end_handle = 0;
    }
    
    /* 重置连接流程标志 */
    table->servers[server_index].mtu_done = false;
    table->servers[server_index].discovery_done = false;
    table->servers[server_index].param_update_done = false;
    table->servers[server_index].auth_complete_received = false;
    
    /* 更新时间戳 */
    table->servers[server_index].state_enter_ms = now_ms();
    
    /* 清除超时 */
    table->servers[server_index].state_timeout_ms = 0;
    
    /* 记录断开原因 */
    table->servers[server_index].disconnect_reason = reason;
    
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_touch_state(sle_server_connections_t *table, int server_index, uint64_t now_ms)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].last_state_ms = now_ms;
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_set_mtu_done(sle_server_connections_t *table, int server_index, bool done)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].mtu_done = done;
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_set_notify_handle(sle_server_connections_t *table, int server_index, uint16_t handle)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].notify_handle = handle;
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_set_write_handle(sle_server_connections_t *table, int server_index, uint16_t handle)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].write_handle = handle;
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_record_rx(sle_server_connections_t *table, int server_index, uint64_t now_ms)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].last_rx_ms = now_ms;
    table->servers[server_index].last_state_ms = now_ms;
    table->servers[server_index].rx_count++;
    pthread_mutex_unlock(&table->mutex);
}

bool server_connections_get_server_copy(sle_server_connections_t *table, int server_index, sle_server_connection_t *out)
{
    if (table == NULL || out == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return false;
    }
    pthread_mutex_lock(&table->mutex);
    *out = table->servers[server_index];
    pthread_mutex_unlock(&table->mutex);
    return true;
}

int server_connections_active_count(sle_server_connections_t *table)
{
    if (table == NULL) {
        return 0;
    }
    pthread_mutex_lock(&table->mutex);
    int count = 0;
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        if (table->servers[i].used &&
            (table->servers[i].state == SLE_SERVER_CONNECTED ||
             table->servers[i].state == SLE_SERVER_PAIRING ||
             table->servers[i].state == SLE_SERVER_DISCOVERING ||
             table->servers[i].state == SLE_SERVER_READY)) {
            count++;
        }
    }
    pthread_mutex_unlock(&table->mutex);
    return count;
}

const char *server_connections_state_name(sle_server_connection_state_t state)
{
    switch (state) {
        case SLE_SERVER_IDLE: return "idle";
        case SLE_SERVER_CONNECTING: return "connecting";
        case SLE_SERVER_CONNECTED: return "connected";
        case SLE_SERVER_PAIRING: return "pairing";
        case SLE_SERVER_DISCOVERING: return "discovering";
        case SLE_SERVER_READY: return "ready";
        case SLE_SERVER_DISCONNECTED: return "disconnected";
        default: return "unknown";
    }
}

void server_connections_addr_to_string(const sle_addr_t *addr, char *buf, uint32_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }
    if (addr == NULL || buf_len < SLE_ADDR_STR_LEN) {
        snprintf(buf, buf_len, "unknown");
        return;
    }
    snprintf(buf, buf_len, "%02x:%02x:%02x:%02x:%02x:%02x",
        addr->addr[0], addr->addr[1], addr->addr[2],
        addr->addr[3], addr->addr[4], addr->addr[5]);
}

/* ==========================================================================
 * 超时管理
 * ========================================================================== */

void server_connections_set_state_timeout(sle_server_connections_t *table, 
    int server_index, uint32_t timeout_ms)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].state_timeout_ms = timeout_ms;
    table->servers[server_index].state_enter_ms = now_ms();
    pthread_mutex_unlock(&table->mutex);
}

bool server_connections_is_state_timeout(sle_server_connections_t *table, 
    int server_index, uint64_t now_ms)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return false;
    }
    pthread_mutex_lock(&table->mutex);
    sle_server_connection_t *server = &table->servers[server_index];
    bool timeout = false;
    
    /* 只有设置了超时且有进入时间才检查 */
    if (server->state_timeout_ms > 0 && server->state_enter_ms > 0) {
        timeout = (now_ms - server->state_enter_ms) >= server->state_timeout_ms;
    }
    
    pthread_mutex_unlock(&table->mutex);
    return timeout;
}

void server_connections_set_param_update_done(sle_server_connections_t *table, 
    int server_index, bool done)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].param_update_done = done;
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_set_auth_complete(sle_server_connections_t *table, 
    int server_index, bool received)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].auth_complete_received = received;
    pthread_mutex_unlock(&table->mutex);
}

/* ==========================================================================
 * 调试辅助
 * ========================================================================== */

void server_connections_dump_table(sle_server_connections_t *table, const char *tag)
{
    if (table == NULL) {
        return;
    }
    
    fprintf(stderr, "[SLE][TABLE] begin tag=%s\n", tag != NULL ? tag : "dump");
    pthread_mutex_lock(&table->mutex);
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        sle_server_connection_t *s = &table->servers[i];
        char addr_str[SLE_ADDR_STR_LEN] = {0};
        server_connections_addr_to_string(&s->addr, addr_str, sizeof(addr_str));
        
        fprintf(stderr, "[SLE][TABLE] index=%u used=%d state=%s conn_id=%u "
            "mac=%s rx=%u param_done=%d reason=0x%x\n",
            i, s->used, server_connections_state_name(s->state),
            s->conn_id, addr_str, s->rx_count, s->param_update_done,
            s->disconnect_reason);
    }
    pthread_mutex_unlock(&table->mutex);
    fprintf(stderr, "[SLE][TABLE] end\n");
}

void server_connections_get_stats(sle_server_connections_t *table,
    uint8_t *out_total, uint8_t *out_active, uint8_t *out_ready)
{
    if (table == NULL) {
        return;
    }
    
    uint8_t total = 0, active = 0, ready = 0;
    pthread_mutex_lock(&table->mutex);
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        if (table->servers[i].used) {
            total++;
            if (table->servers[i].state != SLE_SERVER_IDLE &&
                table->servers[i].state != SLE_SERVER_DISCONNECTED) {
                active++;
            }
            if (table->servers[i].state == SLE_SERVER_READY) {
                ready++;
            }
        }
    }
    pthread_mutex_unlock(&table->mutex);
    
    if (out_total != NULL) *out_total = total;
    if (out_active != NULL) *out_active = active;
    if (out_ready != NULL) *out_ready = ready;
}

/**
 * @brief 合法的状态转换表
 * @note 行=源状态，列=目标状态，1=合法，0=非法
 *       IDLE CONNING CONNED PAIRING DISCING READY DISCONN
 */
static const bool g_valid_transitions[SLE_SERVER_DISCONNECTED + 1][SLE_SERVER_DISCONNECTED + 1] = {
    /* IDLE       */ { 0, 1, 0, 0, 0, 0, 1 },
    /* CONNECTING */ { 0, 0, 1, 0, 0, 0, 1 },
    /* CONNECTED  */ { 0, 0, 0, 1, 1, 0, 1 },
    /* PAIRING    */ { 0, 0, 1, 0, 0, 0, 1 },
    /* DISCOVERING*/ { 0, 0, 0, 0, 0, 1, 1 },
    /* READY      */ { 0, 0, 0, 0, 0, 0, 1 },
    /* DISCONNECTED*/{ 0, 1, 0, 0, 0, 0, 0 },
};

bool server_connections_is_valid_transition(sle_server_connection_state_t from, 
    sle_server_connection_state_t to)
{
    if (from > SLE_SERVER_DISCONNECTED || to > SLE_SERVER_DISCONNECTED) {
        return false;
    }
    return g_valid_transitions[from][to];
}

/* ==========================================================================
 * 快速重连支持
 * ========================================================================== */

void server_connections_set_keep_pair_info(sle_server_connections_t *table, 
    int server_index, bool keep)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].keep_pair_info = keep;
    pthread_mutex_unlock(&table->mutex);
}

void server_connections_set_service_handles(sle_server_connections_t *table, 
    int server_index, uint16_t start_handle, uint16_t end_handle)
{
    if (table == NULL || server_index < 0 || server_index >= (int)table->max_connections) {
        return;
    }
    pthread_mutex_lock(&table->mutex);
    table->servers[server_index].service_start_handle = start_handle;
    table->servers[server_index].service_end_handle = end_handle;
    pthread_mutex_unlock(&table->mutex);
}

int server_connections_find_reconnectable(sle_server_connections_t *table, const sle_addr_t *addr)
{
    if (table == NULL || addr == NULL) {
        return -1;
    }
    pthread_mutex_lock(&table->mutex);
    int found = -1;
    for (uint8_t i = 0; i < table->max_connections; ++i) {
        if (table->servers[i].used && 
            table->servers[i].state == SLE_SERVER_DISCONNECTED &&
            table->servers[i].keep_pair_info &&
            addr_equal(&table->servers[i].addr, addr)) {
            found = (int)i;
            break;
        }
    }
    pthread_mutex_unlock(&table->mutex);
    return found;
}
