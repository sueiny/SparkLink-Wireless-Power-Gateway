#include "sle_multi_client.h"
#include "server_connections.h"
#include "notify_printer.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "securec.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_device_manager.h"
#include "sle_errcode.h"
#include "sle_ssap_client.h"
#include "soc_osal.h"

/*
 * SLE manager module map
 *
 * Public lifecycle:
 *   sle_manager_init(), sle_manager_deinit(), sle_manager_tick()
 * SDK callback registration:
 *   register_callbacks() wires seek/connect/SSAP callback tables.
 * Flow:
 *   scan -> candidate cache -> single link-create -> pair/auth ->
 *   MTU exchange -> service/property discovery -> READY -> notify enqueue.
 *
 * Important invariants:
 *   - Only one sle_connect_remote_device() is in flight at a time.
 *   - READY connections may continue MTU/parameter update work while the
 *     scheduler starts scanning for the next server.
 *   - notify callbacks only validate, count, and enqueue payloads; formatting
 *     and file/stderr output live in notify_printer's worker thread.
 */

#define UUID_16BIT_LEN 2
#define UUID_14_BYTE 14
#define UUID_15_BYTE 15
#define SLE_WAIT_ENABLE_LOOPS 20
#define SLE_WAIT_STEP_US 100000
#define SLE_RESTART_SCAN_DELAY_US 200000
#define SLE_CANDIDATE_STALE_MS 15000

/* 应用层断开原因 */
#define SLE_DISCONNECT_REASON_APP_STALE             0xE001
#define SLE_DISCONNECT_REASON_CONNECTING_TIMEOUT    0xE002
#define SLE_DISCONNECT_REASON_PAIRING_TIMEOUT       0xE003
#define SLE_DISCONNECT_REASON_DISCOVERY_TIMEOUT     0xE004
#define SLE_DISCONNECT_REASON_ACTIVE_LIMIT          0xE005

/* 日志控制 */
#define SLE_VERBOSE_LOG 0
#define SLE_VERBOSE(...) do { if (g_config.enable_verbose_log || SLE_VERBOSE_LOG) { printf(__VA_ARGS__); } } while (0)

static sle_app_config_t g_config;
static sle_server_connections_t g_server_connections;
static sle_announce_seek_callbacks_t g_seek_cbk;
static sle_connection_callbacks_t g_connect_cbk;
static ssapc_callbacks_t g_ssapc_cbk;
static sle_uuid_t g_client_app_uuid = {UUID_16BIT_LEN, {0}};
static uint8_t g_client_id;
static volatile uint8_t g_sle_enabled;
static volatile bool g_running;
static volatile bool g_scan_active;
static bool g_has_pending_connect;
static int g_pending_connect_index = -1;
static sle_addr_t g_pending_connect_addr;
static int g_last_reported_active_count = -1;
static pthread_mutex_t g_core_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_scan_restart_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_scan_restart_pending;

typedef struct {
    bool used;
    sle_addr_t addr;
    int8_t rssi;
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;
    uint64_t failed_until_ms;
    uint64_t connect_start_ms;
    uint32_t fail_count;
} sle_connect_candidate_t;

static sle_connect_candidate_t g_candidates[SLE_DATA_APP_MAX_CONNECTIONS];
static uint64_t g_first_connect_start_ms;

static void request_scan_restart(void);
static void candidate_try_start_best(void);
static bool should_skip_retry_backoff(const sle_server_connection_t *server, uint64_t now);
static int scan_restart_start_worker(void);

/* --------------------------------------------------------------------------
 * Logging and state helpers
 * -------------------------------------------------------------------------- */

static uint64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static void print_connection_table(void)
{
    fprintf(stderr, "[SLE][TABLE] begin\n");
    for (uint8_t i = 0; i < g_config.max_connections; ++i) {
        sle_server_connection_t server;
        if (!server_connections_get_server_copy(&g_server_connections, i, &server) || !server.used) {
            continue;
        }
        char addr[32] = {0};
        server_connections_addr_to_string(&server.addr, addr, sizeof(addr));
        fprintf(stderr, "[SLE][TABLE] server_index=%u state=%s conn_id=%u mac=%s rx_count=%u reason=0x%x\n",
            i, server_connections_state_name(server.state), server.conn_id, addr,
            server.rx_count, server.disconnect_reason);
    }
    fprintf(stderr, "[SLE][TABLE] end\n");
    fflush(stderr);
}

static void print_active_count_if_changed(const char *reason)
{
    int active = server_connections_active_count(&g_server_connections);
    if (active == g_last_reported_active_count) {
        return;
    }
    g_last_reported_active_count = active;
    fprintf(stderr, "[SLE][STATUS] active_connections=%d reason=%s\n",
        active, reason != NULL ? reason : "update");
    print_connection_table();
}

static void print_server_state(const char *event, int server_index, uint16_t conn_id,
    const sle_addr_t *addr, uint32_t reason)
{
    char addr_text[32] = {0};
    server_connections_addr_to_string(addr, addr_text, sizeof(addr_text));
    fprintf(stderr, "[SLE][%s] server_index=%d conn_id=%u mac=%s reason=0x%x\n",
        event, server_index, conn_id, addr_text, reason);
    fflush(stderr);
    print_active_count_if_changed(event);
}

static void mark_server_stale_disconnected(uint8_t server_index, const sle_server_connection_t *server)
{
    if (server == NULL) {
        return;
    }
    (void)sle_disconnect_remote_device(&server->addr);
    server_connections_mark_disconnected(&g_server_connections, server_index, SLE_DISCONNECT_REASON_APP_STALE);
    print_server_state("DISCONNECTED", server_index, server->conn_id, &server->addr, SLE_DISCONNECT_REASON_APP_STALE);
    request_scan_restart();
}

static void ensure_dir(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return;
    }
    char tmp[SLE_DATA_APP_PATH_MAX] = {0};
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* --------------------------------------------------------------------------
 * Target matching helpers
 * -------------------------------------------------------------------------- */

static bool addr_matches_prefix(const sle_addr_t *addr, uint16_t prefix)
{
    if (addr == NULL) {
        return false;
    }
    if (prefix <= 0xFF) {
        return addr->addr[0] == (uint8_t)prefix;
    }
    return addr->addr[0] == (uint8_t)((prefix >> 8) & 0xFF) &&
        addr->addr[1] == (uint8_t)(prefix & 0xFF);
}

static bool addr_equal(const sle_addr_t *a, const sle_addr_t *b)
{
    return a != NULL && b != NULL && a->type == b->type &&
        memcmp(a->addr, b->addr, sizeof(a->addr)) == 0;
}

/* --------------------------------------------------------------------------
 * Candidate scheduler
 * -------------------------------------------------------------------------- */

static int find_candidate_by_addr(const sle_addr_t *addr)
{
    if (addr == NULL) {
        return -1;
    }
    for (uint8_t i = 0; i < SLE_DATA_APP_MAX_CONNECTIONS; ++i) {
        if (g_candidates[i].used && addr_equal(&g_candidates[i].addr, addr)) {
            return (int)i;
        }
    }
    return -1;
}

static int find_candidate_slot(uint64_t now)
{
    int oldest = -1;
    uint64_t oldest_seen = UINT64_MAX;
    for (uint8_t i = 0; i < SLE_DATA_APP_MAX_CONNECTIONS; ++i) {
        if (!g_candidates[i].used) {
            return (int)i;
        }
        if (g_candidates[i].last_seen_ms < oldest_seen) {
            oldest_seen = g_candidates[i].last_seen_ms;
            oldest = (int)i;
        }
    }
    (void)now;
    return oldest;
}

static void candidate_update(const sle_addr_t *addr, int8_t rssi, uint64_t now)
{
    if (addr == NULL) {
        return;
    }
    int index = find_candidate_by_addr(addr);
    if (index < 0) {
        index = find_candidate_slot(now);
    }
    if (index < 0) {
        return;
    }
    if (!g_candidates[index].used || !addr_equal(&g_candidates[index].addr, addr)) {
        memset(&g_candidates[index], 0, sizeof(g_candidates[index]));
        g_candidates[index].addr = *addr;
        g_candidates[index].first_seen_ms = now;
    }
    g_candidates[index].used = true;
    g_candidates[index].rssi = rssi;
    g_candidates[index].last_seen_ms = now;
}

static void candidate_mark_failed(const sle_addr_t *addr, uint64_t now)
{
    int index = find_candidate_by_addr(addr);
    if (index < 0) {
        return;
    }
    g_candidates[index].fail_count++;
    if (g_config.connect_fail_penalty_ms > 0) {
        g_candidates[index].failed_until_ms = now + g_config.connect_fail_penalty_ms;
    }
}

static void candidate_mark_connected(const sle_addr_t *addr)
{
    int index = find_candidate_by_addr(addr);
    if (index < 0) {
        return;
    }
    g_candidates[index].failed_until_ms = 0;
    g_candidates[index].fail_count = 0;
}

static uint64_t candidate_connect_start(const sle_addr_t *addr)
{
    int index = find_candidate_by_addr(addr);
    return index >= 0 ? g_candidates[index].connect_start_ms : 0;
}

static bool candidate_is_connectable(int index, uint64_t now)
{
    if (index < 0 || index >= SLE_DATA_APP_MAX_CONNECTIONS || !g_candidates[index].used) {
        return false;
    }
    if (now - g_candidates[index].last_seen_ms > SLE_CANDIDATE_STALE_MS) {
        return false;
    }
    if (g_candidates[index].failed_until_ms > now) {
        return false;
    }

    int server_index = server_connections_find_by_addr(&g_server_connections, &g_candidates[index].addr);
    if (server_index < 0) {
        return true;
    }

    sle_server_connection_t server;
    if (!server_connections_get_server_copy(&g_server_connections, server_index, &server)) {
        return false;
    }
    if (should_skip_retry_backoff(&server, now)) {
        return false;
    }
    return server.state == SLE_SERVER_IDLE || server.state == SLE_SERVER_DISCONNECTED;
}

static int candidate_choose_best(uint64_t now)
{
    int best = -1;
    for (uint8_t i = 0; i < SLE_DATA_APP_MAX_CONNECTIONS; ++i) {
        if (!candidate_is_connectable((int)i, now)) {
            continue;
        }
        if (best < 0 ||
            g_candidates[i].fail_count < g_candidates[best].fail_count ||
            (g_candidates[i].fail_count == g_candidates[best].fail_count &&
                g_candidates[i].rssi > g_candidates[best].rssi) ||
            (g_candidates[i].fail_count == g_candidates[best].fail_count &&
                g_candidates[i].rssi == g_candidates[best].rssi &&
                g_candidates[i].last_seen_ms > g_candidates[best].last_seen_ms)) {
            best = (int)i;
        }
    }
    return best;
}

static bool seek_result_matches_target(const sle_seek_result_info_t *result)
{
    if (result == NULL) {
        return false;
    }
    return addr_matches_prefix(&result->addr, g_config.mac_prefix);
}

/* --------------------------------------------------------------------------
 * SSAP property helpers
 * -------------------------------------------------------------------------- */

static uint16_t property_uuid16(const ssapc_find_property_result_t *property)
{
    if (property == NULL || property->uuid.len <= UUID_15_BYTE) {
        return 0;
    }
    return (uint16_t)(((uint16_t)property->uuid.uuid[UUID_15_BYTE] << 8) | property->uuid.uuid[UUID_14_BYTE]);
}

static bool property_supports_data_channel(const ssapc_find_property_result_t *property)
{
    if (property == NULL) {
        return false;
    }
    bool can_receive = (property->operate_indication & SSAP_OPERATE_INDICATION_BIT_NOTIFY) != 0 ||
        (property->operate_indication & SSAP_OPERATE_INDICATION_BIT_INDICATE) != 0;
    bool can_write = (property->operate_indication & SSAP_OPERATE_INDICATION_BIT_WRITE) != 0 ||
        (property->operate_indication & SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP) != 0;
    return can_receive && can_write;
}

static bool is_retryable_failure(uint32_t reason)
{
    return reason == SLE_DISCONNECT_REASON_CONNECTING_TIMEOUT ||
        reason == SLE_DISCONNECT_REASON_PAIRING_TIMEOUT ||
        reason == SLE_DISCONNECT_REASON_DISCOVERY_TIMEOUT;
}

static bool should_skip_retry_backoff(const sle_server_connection_t *server, uint64_t now)
{
    if (server == NULL || server->state != SLE_SERVER_DISCONNECTED ||
        g_config.connect_retry_backoff_ms == 0 || !is_retryable_failure(server->disconnect_reason)) {
        return false;
    }
    return server->state_enter_ms > 0 && now - server->state_enter_ms < g_config.connect_retry_backoff_ms;
}

/* --------------------------------------------------------------------------
 * Scan and connect helpers
 * -------------------------------------------------------------------------- */

static uint8_t target_active_limit(void)
{
    uint8_t limit = g_config.target_active_connections;
    if (limit == 0 || limit > g_config.max_connections) {
        limit = g_config.max_connections;
    }
    return limit;
}

static bool has_active_capacity(void)
{
    return server_connections_active_count(&g_server_connections) < target_active_limit() &&
        server_connections_has_capacity(&g_server_connections);
}

/**
 * @brief 检查是否有正在建立中的连接
 * @note link create 不能并发推进；否则容易触发 0x1401 超时并污染连接表
 */
static bool has_blocking_server(void)
{
    for (uint8_t i = 0; i < g_config.max_connections; ++i) {
        sle_server_connection_t server;
        if (!server_connections_get_server_copy(&g_server_connections, i, &server)) {
            continue;
        }
        if (server.used &&
            (server.state == SLE_SERVER_CONNECTING || server.state == SLE_SERVER_PAIRING)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 执行扫描重启
 * @note 扫描重启放到短后台任务里，避免在 SDK 回调线程内睡眠阻塞后续 HCI/SSAP 事件
 * @note 一次只发起一个新连接；已连接设备可继续完成 MTU/发现/参数更新
 */
static void do_scan_restart(void)
{
    if (!g_running) {
        return;
    }
    candidate_try_start_best();
    if (g_has_pending_connect || has_blocking_server()) {
        return;
    }
    if (g_scan_active) {
        return;
    }
    if (!g_config.continue_scan_when_full && !has_active_capacity()) {
        SLE_VERBOSE("[SLE][SCAN] skip restart: connection table full\n");
        return;
    }
    errcode_t ret = sle_start_seek();
    g_scan_active = (ret == ERRCODE_SLE_SUCCESS);
    SLE_VERBOSE("[SLE][SCAN] restart ret=%d active=%d\n", ret, g_scan_active ? 1 : 0);
}

static void *scan_restart_worker(void *arg)
{
    (void)arg;
    /* 使用配置的扫描重启延迟 */
    uint32_t delay_us = g_config.scan_restart_delay_ms > 0 ? 
        g_config.scan_restart_delay_ms * 1000 : SLE_RESTART_SCAN_DELAY_US;
    usleep(delay_us);
    do_scan_restart();
    pthread_mutex_lock(&g_scan_restart_lock);
    g_scan_restart_pending = false;
    pthread_mutex_unlock(&g_scan_restart_lock);
    return NULL;
}

static void request_scan_restart(void)
{
    if (g_has_pending_connect || has_blocking_server()) {
        return;
    }
    pthread_mutex_lock(&g_scan_restart_lock);
    if (g_scan_restart_pending) {
        pthread_mutex_unlock(&g_scan_restart_lock);
        return;
    }
    g_scan_restart_pending = true;
    pthread_mutex_unlock(&g_scan_restart_lock);

    if (scan_restart_start_worker() != 0) {
        pthread_mutex_lock(&g_scan_restart_lock);
        g_scan_restart_pending = false;
        pthread_mutex_unlock(&g_scan_restart_lock);
        SLE_VERBOSE("[SLE][WARN] create scan restart worker failed\n");
        return;
    }
}

/* 使用已跑通的 WS73 Linux 扫描参数，不在第一版打开多 PHY 或复杂过滤。 */
static void start_scan(void)
{
    sle_seek_param_t param = {0};
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = SLE_SEEK_FILTER_ALLOW_ALL;
    param.seek_phys = SLE_SEEK_PHY_1M;
    param.seek_type[0] = SLE_SEEK_ACTIVE;
    param.seek_interval[0] = g_config.scan_interval;
    param.seek_window[0] = g_config.scan_window;
    errcode_t ret = sle_set_seek_param(&param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        SLE_VERBOSE("[SLE][ERROR] set seek param failed ret=%d\n", ret);
        return;
    }
    ret = sle_start_seek();
    g_scan_active = (ret == ERRCODE_SLE_SUCCESS);
    SLE_VERBOSE("[SLE][SCAN] start ret=%d active=%d\n", ret, g_scan_active ? 1 : 0);
}

static bool prepare_pending_connect(const sle_addr_t *addr, uint64_t now)
{
    if (addr == NULL || !has_active_capacity()) {
        return false;
    }

    bool is_new = false;
    int server_index = server_connections_find_reconnectable(&g_server_connections, addr);
    if (server_index < 0) {
        server_index = server_connections_alloc_or_reuse(&g_server_connections, addr, &is_new);
    }
    if (server_index < 0) {
        return false;
    }

    sle_server_connection_t copy;
    server_connections_get_server_copy(&g_server_connections, server_index, &copy);
    if (should_skip_retry_backoff(&copy, now)) {
        return false;
    }
    if (copy.state == SLE_SERVER_CONNECTING || copy.state == SLE_SERVER_CONNECTED ||
        copy.state == SLE_SERVER_PAIRING || copy.state == SLE_SERVER_DISCOVERING ||
        copy.state == SLE_SERVER_READY) {
        return false;
    }

    server_connections_mark_connecting(&g_server_connections, server_index, addr);
    if (g_config.connecting_timeout_ms > 0) {
        server_connections_set_state_timeout(&g_server_connections, server_index, g_config.connecting_timeout_ms);
    }
    g_pending_connect_addr = *addr;
    g_pending_connect_index = server_index;
    g_has_pending_connect = true;
    return true;
}

static void issue_pending_connect(void)
{
    if (!g_has_pending_connect || g_pending_connect_index < 0) {
        return;
    }
    char addr[32] = {0};
    server_connections_addr_to_string(&g_pending_connect_addr, addr, sizeof(addr));
    uint64_t now = now_ms();
    int candidate_index = find_candidate_by_addr(&g_pending_connect_addr);
    if (candidate_index >= 0) {
        g_candidates[candidate_index].connect_start_ms = now;
    }
    if (g_first_connect_start_ms == 0) {
        g_first_connect_start_ms = now;
    }
    fprintf(stderr, "[SLE][TIMING] connect_start server_index=%d mac=%s since_first_ms=%llu\n",
        g_pending_connect_index, addr,
        (unsigned long long)(now - g_first_connect_start_ms));
    errcode_t ret = sle_connect_remote_device(&g_pending_connect_addr);
    SLE_VERBOSE("[SLE][CONN] connect server_index=%d addr=%s ret=%d\n", g_pending_connect_index, addr, ret);
    if (ret != ERRCODE_SLE_SUCCESS) {
        candidate_mark_failed(&g_pending_connect_addr, now_ms());
        server_connections_mark_disconnected(&g_server_connections, g_pending_connect_index, (uint32_t)ret);
        g_has_pending_connect = false;
        g_pending_connect_index = -1;
        candidate_try_start_best();
        request_scan_restart();
        return;
    }
    g_has_pending_connect = false;
    g_pending_connect_index = -1;
}

static void candidate_try_start_best(void)
{
    if (g_has_pending_connect || has_blocking_server() || !has_active_capacity()) {
        return;
    }

    uint64_t now = now_ms();
    int candidate_index = candidate_choose_best(now);
    if (candidate_index < 0) {
        return;
    }
    if (!prepare_pending_connect(&g_candidates[candidate_index].addr, now)) {
        return;
    }
    if (g_scan_active) {
        errcode_t ret = sle_stop_seek();
        SLE_VERBOSE("[SLE][SCAN] stop for cached connect server_index=%d ret=%d\n",
            g_pending_connect_index, ret);
        if (ret != ERRCODE_SLE_SUCCESS) {
            server_connections_mark_disconnected(&g_server_connections, g_pending_connect_index, (uint32_t)ret);
            g_has_pending_connect = false;
            g_pending_connect_index = -1;
            request_scan_restart();
        }
        return;
    }
    issue_pending_connect();
}

static void request_exchange_info(uint16_t conn_id)
{
    ssap_exchange_info_t info = {0};
    info.mtu_size = g_config.mtu;
    info.version = 1;
    errcode_t ret = ssapc_exchange_info_req(g_client_id, conn_id, &info);
    SLE_VERBOSE("[SLE][SSAP] exchange info req conn_id=%u mtu=%u ret=%d\n", conn_id, g_config.mtu, ret);
}

/* 每个 conn_id 都要独立发现；重连后旧 handle 不可信，不能复用。 */
static void start_property_discovery(uint16_t conn_id)
{
    ssapc_find_structure_param_t find_param = {0};
    find_param.type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    errcode_t ret = ssapc_find_structure(g_client_id, conn_id, &find_param);
    SLE_VERBOSE("[SLE][SSAP] discover primary service conn_id=%u ret=%d\n", conn_id, ret);
}

static void start_property_only_discovery(uint16_t conn_id)
{
    ssapc_find_structure_param_t find_param = {0};
    find_param.type = SSAP_FIND_TYPE_PROPERTY;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    errcode_t ret = ssapc_find_structure(g_client_id, conn_id, &find_param);
    SLE_VERBOSE("[SLE][SSAP] discover property conn_id=%u ret=%d\n", conn_id, ret);
}

/* --------------------------------------------------------------------------
 * SDK callbacks
 * -------------------------------------------------------------------------- */

static void set_default_connection_params(void)
{
    sle_default_connect_param_t param = {0};
    param.enable_filter_policy = 0;
    param.gt_negotiate = SLE_ANNOUNCE_ROLE_G_CAN_NEGO;
    param.initiate_phys = SLE_SEEK_PHY_1M;
    param.max_interval = g_config.conn_interval;
    param.min_interval = g_config.conn_interval;
    param.scan_interval = g_config.conn_scan_interval;
    param.scan_window = g_config.conn_scan_window;
    param.timeout = g_config.supervision_timeout;
    errcode_t ret = sle_default_connection_param_set(&param);
    SLE_VERBOSE("[SLE][CONN] set default conn param ret=%d scan=%u/%u interval=0x%x timeout=0x%x\n",
        ret, g_config.conn_scan_interval, g_config.conn_scan_window,
        g_config.conn_interval, g_config.supervision_timeout);
}

static errcode_t request_connection_param_update(uint16_t conn_id)
{
    sle_connection_param_update_t params = {0};
    params.conn_id = conn_id;
    params.interval_min = g_config.conn_interval;
    params.interval_max = g_config.conn_interval;
    params.max_latency = g_config.conn_latency;
    params.supervision_timeout = g_config.supervision_timeout;
    if (g_config.conn_latency == 0xFFFF) {
        return ERRCODE_SLE_SUCCESS;
    }
    errcode_t ret = sle_update_connect_param(&params);
    SLE_VERBOSE("[SLE][CONN] update request conn_id=%u ret=%d interval=0x%x latency=0x%x timeout=0x%x\n",
        conn_id, ret, g_config.conn_interval, g_config.conn_latency, g_config.supervision_timeout);
    return ret;
}

/* SLE 协议栈 enable 后再注册 SSAP client 并启动扫描，贴近已验证样例流程。 */
static void sle_enable_cb(errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS) {
        printf("[SLE][ERROR] enable failed status=%d\n", status);
        return;
    }
    g_sle_enabled = 1;
    SLE_VERBOSE("[SLE] enable success\n");
    set_default_connection_params();
    errcode_t ret = ssapc_register_client(&g_client_app_uuid, &g_client_id);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE][ERROR] register ssap client failed ret=%d\n", ret);
        return;
    }
    SLE_VERBOSE("[SLE][SSAP] client registered id=%u\n", g_client_id);
    start_scan();
}

static void sle_disable_cb(errcode_t status)
{
    SLE_VERBOSE("[SLE] disable status=%d\n", status);
    g_sle_enabled = 0;
}

static void seek_enable_cb(errcode_t status)
{
    SLE_VERBOSE("[SLE][SCAN] enable status=%d\n", status);
}

static void seek_disable_cb(errcode_t status)
{
    SLE_VERBOSE("[SLE][SCAN] disable status=%d\n", status);
    g_scan_active = false;
    if (status != ERRCODE_SLE_SUCCESS || !g_has_pending_connect) {
        return;
    }

    if (!has_active_capacity()) {
        if (g_pending_connect_index >= 0) {
            server_connections_mark_disconnected(&g_server_connections, g_pending_connect_index,
                SLE_DISCONNECT_REASON_ACTIVE_LIMIT);
        }
        g_has_pending_connect = false;
        g_pending_connect_index = -1;
        return;
    }

    issue_pending_connect();
}

/* 扫描命中后先缓存候选；真正 connect 由候选调度器按优先级推进。 */
static void seek_result_cb(sle_seek_result_info_t *result)
{
    if (result == NULL) {
        return;
    }
    if (!seek_result_matches_target(result)) {
        return;
    }
    if (!has_active_capacity()) {
        return;
    }

    uint64_t now = now_ms();
    char addr[32] = {0};
    server_connections_addr_to_string(&result->addr, addr, sizeof(addr));
    candidate_update(&result->addr, result->rssi, now);
    SLE_VERBOSE("[SLE][SCAN] candidate addr=%s rssi=%d\n", addr, result->rssi);
    candidate_try_start_best();
}

static void connect_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr, sle_acb_state_t conn_state,
    sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    int server_index = -1;
    if (conn_state == SLE_ACB_STATE_CONNECTED && addr != NULL) {
        bool is_new = false;
        server_index = server_connections_alloc_or_reuse(&g_server_connections, addr, &is_new);
        if (server_index >= 0) {
            server_connections_mark_connected(&g_server_connections, server_index, conn_id, (uint8_t)pair_state);
        }
    } else {
        if (addr != NULL) {
            server_index = server_connections_find_by_addr(&g_server_connections, addr);
        }
        if (server_index < 0) {
            server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
        }
    }
    if (server_index >= 0) {
        server_connections_touch_state(&g_server_connections, server_index, now_ms());
    }

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        if (server_index < 0 || addr == NULL) {
            SLE_VERBOSE("[SLE][WARN] connected but no server_index conn_id=%u\n", conn_id);
            return;
        }
        uint64_t now = now_ms();
        uint64_t start = candidate_connect_start(addr);
        char addr_text[32] = {0};
        server_connections_addr_to_string(addr, addr_text, sizeof(addr_text));
        candidate_mark_connected(addr);
        fprintf(stderr, "[SLE][TIMING] connect_done server_index=%d conn_id=%u mac=%s link_ms=%llu since_first_ms=%llu\n",
            server_index, conn_id, addr_text,
            (unsigned long long)(start > 0 ? now - start : 0),
            (unsigned long long)(g_first_connect_start_ms > 0 ? now - g_first_connect_start_ms : 0));
        
        /* 检查是否有保留的配对信息（快速重连） */
        sle_server_connection_t copy;
        server_connections_get_server_copy(&g_server_connections, server_index, &copy);
        bool is_reconnect = copy.keep_pair_info;
        
        print_server_state("CONNECTED", server_index, conn_id, addr, 0);
        if (server_connections_active_count(&g_server_connections) >= target_active_limit()) {
            (void)sle_stop_seek();
            g_scan_active = false;
        }
        
        /* 快速重连：跳过pair，直接进入exchange_info */
        if (is_reconnect && pair_state != SLE_PAIR_NONE) {
            SLE_VERBOSE("[SLE][FAST-RECONNECT] skip pair, go to exchange_info server_index=%d conn_id=%u\n", 
                server_index, conn_id);
            if (g_config.discovery_timeout_ms > 0) {
                server_connections_set_state_timeout(&g_server_connections, server_index, 
                    g_config.discovery_timeout_ms);
            }
            request_exchange_info(conn_id);
        } else if (pair_state == SLE_PAIR_NONE) {
            server_connections_mark_pairing(&g_server_connections, server_index);
            /* 设置pairing超时 */
            if (g_config.pairing_timeout_ms > 0) {
                server_connections_set_state_timeout(&g_server_connections, server_index, 
                    g_config.pairing_timeout_ms);
            }
            errcode_t ret = sle_pair_remote_device(addr);
            SLE_VERBOSE("[SLE][PAIR] pair request server_index=%d conn_id=%u ret=%d\n", server_index, conn_id, ret);
        } else {
            /* 已配对，直接进入exchange_info */
            if (g_config.discovery_timeout_ms > 0) {
                server_connections_set_state_timeout(&g_server_connections, server_index, 
                    g_config.discovery_timeout_ms);
            }
            request_exchange_info(conn_id);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        if (server_index >= 0) {
            sle_server_connection_t server;
            bool has_server = server_connections_get_server_copy(&g_server_connections, server_index, &server);
            if (addr != NULL && server.state == SLE_SERVER_CONNECTING) {
                candidate_mark_failed(addr, now_ms());
            }
            server_connections_mark_disconnected(&g_server_connections, server_index, (uint32_t)disc_reason);
            print_server_state("DISCONNECTED", server_index, conn_id,
                addr != NULL ? addr : (has_server ? &server.addr : NULL), (uint32_t)disc_reason);
        }
        candidate_try_start_best();
        request_scan_restart();
    }
}

/**
 * @brief 认证完成回调
 * @note 配对失败时删除旧配对信息，避免后续一直复用坏 key
 */
static void auth_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status,
    const sle_auth_info_evt_t *evt)
{
    (void)evt;
    int server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
    if (server_index >= 0) {
        server_connections_touch_state(&g_server_connections, server_index, now_ms());
        server_connections_set_auth_complete(&g_server_connections, server_index, true);
    }
    SLE_VERBOSE("[SLE][AUTH] complete server_index=%d conn_id=%u status=%d\n", server_index, conn_id, status);
    if (status != ERRCODE_SLE_SUCCESS && addr != NULL) {
        sle_remove_paired_remote_device(addr);
        if (server_index >= 0) {
            server_connections_mark_disconnected(&g_server_connections, server_index, (uint32_t)status);
        }
        request_scan_restart();
    }
}

static void pair_complete_cb(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    int server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
    if (server_index >= 0) {
        server_connections_touch_state(&g_server_connections, server_index, now_ms());
    }
    SLE_VERBOSE("[SLE][PAIR] complete server_index=%d conn_id=%u status=%d\n", server_index, conn_id, status);
    if (status == ERRCODE_SLE_SUCCESS) {
        /* 配对成功，进入discovery阶段，设置discovery超时 */
        if (server_index >= 0 && g_config.discovery_timeout_ms > 0) {
            server_connections_set_state_timeout(&g_server_connections, server_index, 
                g_config.discovery_timeout_ms);
        }
        request_exchange_info(conn_id);
        return;
    }
    /* 配对失败，清理旧配对信息 */
    if (addr != NULL) {
        sle_remove_paired_remote_device(addr);
    }
    if (server_index >= 0) {
        server_connections_mark_disconnected(&g_server_connections, server_index, (uint32_t)status);
    }
    request_scan_restart();
}

/**
 * @brief 连接参数更新回调
 * @note 参数更新完成后标记完成，允许启动新扫描
 */
static void connect_param_update_cb(uint16_t conn_id, errcode_t status, const sle_connection_param_update_evt_t *param)
{
    int server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
    sle_server_connection_t server;
    bool has_server = false;
    if (server_index >= 0) {
        server_connections_touch_state(&g_server_connections, server_index, now_ms());
        /* 标记参数更新完成 */
        server_connections_set_param_update_done(&g_server_connections, server_index, true);
        has_server = server_connections_get_server_copy(&g_server_connections, server_index, &server);
    }
    uint16_t interval = param != NULL ? param->interval : 0;
    SLE_VERBOSE("[SLE][CONN] update conn_id=%u status=%d interval=0x%x\n", conn_id, status, interval);
    if (server_index >= 0) {
        server_connections_set_state_timeout(&g_server_connections, server_index, 0);
    }
    if (status != ERRCODE_SLE_SUCCESS) {
        fprintf(stderr, "[SLE][WARN] param update failed server_index=%d conn_id=%u status=%d, continue scan\n",
            server_index, conn_id, status);
    }
    /* 参数更新回调无论成功失败都解除补扫门控；失败只影响链路参数，不应卡住第三个连接。 */
    if (has_server && server.state == SLE_SERVER_READY && has_active_capacity()) {
        request_scan_restart();
    }
}

static void set_phy_cb(uint16_t conn_id, errcode_t status, const sle_set_phy_t *param)
{
    (void)param;
    SLE_VERBOSE("[SLE][CONN] set phy callback conn_id=%u status=%d\n", conn_id, status);
}

/**
 * @brief MTU交换完成回调
 * @note MTU 完成后才进入服务发现；发现结果按 conn_id 写回对应 server
 */
static void exchange_info_cb(uint8_t client_id, uint16_t conn_id, ssap_exchange_info_t *param, errcode_t status)
{
    int server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
    uint16_t mtu = param != NULL ? param->mtu_size : 0;
    SLE_VERBOSE("[SLE][SSAP] exchange info cb client=%u conn_id=%u server_index=%d status=%d mtu=%u\n",
        client_id, conn_id, server_index, status, mtu);
    if (server_index < 0 || status != ERRCODE_SLE_SUCCESS) {
        return;
    }
    server_connections_touch_state(&g_server_connections, server_index, now_ms());
    server_connections_set_mtu_done(&g_server_connections, server_index, true);
    server_connections_mark_discovering(&g_server_connections, server_index);
    /* 设置discovery超时 */
    if (g_config.discovery_timeout_ms > 0) {
        server_connections_set_state_timeout(&g_server_connections, server_index, 
            g_config.discovery_timeout_ms);
    }
    start_property_discovery(conn_id);
}

static void find_service_cb(uint8_t client_id, uint16_t conn_id, ssapc_find_service_result_t *service,
    errcode_t status)
{
    (void)client_id;
    if (service == NULL) {
        SLE_VERBOSE("[SLE][WARN] find service null conn_id=%u status=%d\n", conn_id, status);
        return;
    }
    SLE_VERBOSE("[SLE][SSAP] service conn_id=%u status=%d start=0x%x end=0x%x uuid_len=%u\n",
        conn_id, status, service->start_hdl, service->end_hdl, service->uuid.len);
    
    /* 保存服务句柄用于快速重连 */
    int server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
    if (server_index >= 0) {
        server_connections_set_service_handles(&g_server_connections, server_index, 
            service->start_hdl, service->end_hdl);
    }
    
    start_property_only_discovery(conn_id);
}

static void find_complete_cb(uint8_t client_id, uint16_t conn_id, ssapc_find_structure_result_t *structure_result,
    errcode_t status)
{
    (void)structure_result;
    int server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
    SLE_VERBOSE("[SLE][SSAP] find complete client=%u conn_id=%u server_index=%d status=%d\n",
        client_id, conn_id, server_index, status);
    if (server_index >= 0) {
        sle_server_connection_t server;
        server_connections_touch_state(&g_server_connections, server_index, now_ms());
        server_connections_mark_ready(&g_server_connections, server_index);
        if (server_connections_get_server_copy(&g_server_connections, server_index, &server)) {
            uint64_t start = candidate_connect_start(&server.addr);
            uint64_t now = now_ms();
            fprintf(stderr, "[SLE][TIMING] ready server_index=%d conn_id=%u total_ms=%llu since_first_ms=%llu\n",
                server_index, conn_id,
                (unsigned long long)(start > 0 ? now - start : 0),
                (unsigned long long)(g_first_connect_start_ms > 0 ? now - g_first_connect_start_ms : 0));
            print_server_state("READY", server_index, conn_id, &server.addr, 0);
        }
        if (server_connections_active_count(&g_server_connections) >= target_active_limit()) {
            (void)sle_stop_seek();
            g_scan_active = false;
        }
        errcode_t update_ret = request_connection_param_update(conn_id);
        if (g_config.wait_param_update_before_scan && g_config.param_update_timeout_ms > 0 &&
            update_ret == ERRCODE_SLE_SUCCESS && g_config.conn_latency != 0xFFFF) {
            server_connections_set_state_timeout(&g_server_connections, server_index,
                g_config.param_update_timeout_ms);
        } else {
            server_connections_set_state_timeout(&g_server_connections, server_index, 0);
        }
        if (update_ret != ERRCODE_SLE_SUCCESS || g_config.conn_latency == 0xFFFF) {
            server_connections_set_param_update_done(&g_server_connections, server_index, true);
            if (update_ret != ERRCODE_SLE_SUCCESS) {
                fprintf(stderr, "[SLE][WARN] param update request failed server_index=%d conn_id=%u ret=%d, continue scan\n",
                    server_index, conn_id, update_ret);
            }
            if (has_active_capacity()) {
                request_scan_restart();
            }
            return;
        }
        if (!g_config.wait_param_update_before_scan &&
            has_active_capacity()) {
            request_scan_restart();
        }
    }
}

static void find_property_cb(uint8_t client_id, uint16_t conn_id, ssapc_find_property_result_t *property,
    errcode_t status)
{
    (void)client_id;
    int server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
    if (server_index < 0 || property == NULL) {
        SLE_VERBOSE("[SLE][WARN] property without server_index conn_id=%u status=%d\n", conn_id, status);
        return;
    }
    server_connections_touch_state(&g_server_connections, server_index, now_ms());
    uint16_t uuid16 = property_uuid16(property);
    bool uuid_matched = (uuid16 == g_config.data_property_uuid);
    bool auto_matched = g_config.auto_select_data_property && property_supports_data_channel(property);
    SLE_VERBOSE("[SLE][SSAP] property server_index=%d conn_id=%u handle=0x%x uuid16=0x%04x op=0x%x status=%d\n",
        server_index, conn_id, property->handle, uuid16, property->operate_indication, status);
    if (uuid_matched || auto_matched) {
        server_connections_set_notify_handle(&g_server_connections, server_index, property->handle);
        server_connections_set_write_handle(&g_server_connections, server_index, property->handle);
        errcode_t ret = ssapc_read_req(client_id, conn_id, property->handle, SSAP_PROPERTY_TYPE_VALUE);
        SLE_VERBOSE("[SLE][SSAP] data property found server_index=%d uuid16=0x%04x handle=0x%x match=%s read_ret=%d\n",
            server_index, uuid16, property->handle, uuid_matched ? "uuid" : "auto", ret);
    }
}

static void write_confirm_cb(uint8_t client_id, uint16_t conn_id, ssapc_write_result_t *write_result,
    errcode_t status)
{
    uint16_t handle = write_result != NULL ? write_result->handle : 0;
    SLE_VERBOSE("[SLE][SSAP] write cfm client=%u conn_id=%u handle=0x%x status=%d\n",
        client_id, conn_id, handle, status);
}

static void read_confirm_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *read_data, errcode_t status)
{
    uint16_t handle = read_data != NULL ? read_data->handle : 0;
    uint16_t len = read_data != NULL ? read_data->data_len : 0;
    SLE_VERBOSE("[SLE][SSAP] read cfm client=%u conn_id=%u handle=0x%x len=%u status=%d\n",
        client_id, conn_id, handle, len, status);
}

/* notify/indication 是第一阶段真实数据入口：先校验 conn_id，再打印原始数据。 */
static void notification_cb(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id;
    int server_index = server_connections_find_by_conn_id(&g_server_connections, conn_id);
    if (server_index < 0) {
        SLE_VERBOSE("[SLE][WARN] notification unknown conn_id=%u status=%d\n", conn_id, status);
        return;
    }
    if (data == NULL || data->data == NULL || data->data_len == 0) {
        SLE_VERBOSE("[SLE][WARN] notification empty server_index=%d conn_id=%u status=%d\n", server_index, conn_id, status);
        return;
    }
    server_connections_record_rx(&g_server_connections, server_index, now_ms());

    sle_server_connection_t server;
    if (server_connections_get_server_copy(&g_server_connections, server_index, &server)) {
        (void)notify_printer_enqueue_packet(server_index, &server, data->data, data->data_len);
    }
}

static void register_callbacks(void)
{
    memset(&g_seek_cbk, 0, sizeof(g_seek_cbk));
    memset(&g_connect_cbk, 0, sizeof(g_connect_cbk));
    memset(&g_ssapc_cbk, 0, sizeof(g_ssapc_cbk));

    /* Discovery and scan lifecycle callbacks. */
    g_seek_cbk.sle_enable_cb = sle_enable_cb;
    g_seek_cbk.sle_disable_cb = sle_disable_cb;
    g_seek_cbk.seek_enable_cb = seek_enable_cb;
    g_seek_cbk.seek_disable_cb = seek_disable_cb;
    g_seek_cbk.seek_result_cb = seek_result_cb;

    /* Link, pairing, parameter update, and health callbacks. */
    g_connect_cbk.connect_state_changed_cb = connect_state_changed_cb;
    g_connect_cbk.auth_complete_cb = auth_complete_cb;
    g_connect_cbk.pair_complete_cb = pair_complete_cb;
    g_connect_cbk.connect_param_update_cb = connect_param_update_cb;
    g_connect_cbk.set_phy_cb = set_phy_cb;

    /* SSAP discovery and data callbacks. */
    g_ssapc_cbk.exchange_info_cb = exchange_info_cb;
    g_ssapc_cbk.find_structure_cb = find_service_cb;
    g_ssapc_cbk.find_structure_cmp_cb = find_complete_cb;
    g_ssapc_cbk.ssapc_find_property_cbk = find_property_cb;
    g_ssapc_cbk.write_cfm_cb = write_confirm_cb;
    g_ssapc_cbk.read_cfm_cb = read_confirm_cb;
    g_ssapc_cbk.notification_cb = notification_cb;
    g_ssapc_cbk.indication_cb = notification_cb;

    sle_announce_seek_register_callbacks(&g_seek_cbk);
    sle_connection_register_callbacks(&g_connect_cbk);
    ssapc_register_callbacks(&g_ssapc_cbk);
}

/* --------------------------------------------------------------------------
 * Public lifecycle
 * -------------------------------------------------------------------------- */

int sle_manager_init(const sle_app_config_t *config)
{
    if (config == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_core_lock);
    g_config = *config;
    g_running = true;
    g_sle_enabled = 0;
    g_scan_active = false;
    g_has_pending_connect = false;
    g_pending_connect_index = -1;
    g_first_connect_start_ms = 0;
    memset(g_candidates, 0, sizeof(g_candidates));
    server_connections_init(&g_server_connections, g_config.max_connections);
    register_callbacks();
    ensure_dir(g_config.persistence_path);
    errcode_t ret = sle_dev_manager_register_file_path((const uint8_t *)g_config.persistence_path);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE][WARN] register persistence path failed path=%s ret=%d\n", g_config.persistence_path, ret);
    }
    ret = enable_sle();
    pthread_mutex_unlock(&g_core_lock);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[SLE][ERROR] enable_sle ret=%d\n", ret);
        return -1;
    }
    for (int i = 0; i < SLE_WAIT_ENABLE_LOOPS; ++i) {
        if (g_sle_enabled) {
            break;
        }
        usleep(SLE_WAIT_STEP_US);
    }
    return g_sle_enabled ? 0 : -1;
}

void sle_manager_deinit(void)
{
    pthread_mutex_lock(&g_core_lock);
    g_running = false;
    sle_stop_seek();
    if (g_client_id != 0) {
        ssapc_unregister_client(g_client_id);
    }
    disable_sle();
    pthread_mutex_unlock(&g_core_lock);
    usleep(SLE_RESTART_SCAN_DELAY_US + 50000);
    for (int i = 0; i < SLE_WAIT_ENABLE_LOOPS; ++i) {
        if (!g_sle_enabled) {
            break;
        }
        usleep(SLE_WAIT_STEP_US);
    }
    server_connections_deinit(&g_server_connections);
}

/* --------------------------------------------------------------------------
 * Thread entry points
 * -------------------------------------------------------------------------- */

static int scan_restart_start_worker(void)
{
    pthread_t tid;
    int ret = pthread_create(&tid, NULL, scan_restart_worker, NULL);
    if (ret != 0) {
        return -1;
    }
    pthread_detach(tid);
    return 0;
}

/**
 * @brief 维护线程周期调用，处理连接流程超时和业务空闲超时
 * @note 底层断链以 connect_state_changed_cb(DISCONNECTED) 为准；这里不做主动 RSSI 探测。
 */
void sle_manager_tick(void)
{
    uint64_t now = now_ms();
    bool need_scan_restart = false;
    
    for (uint8_t i = 0; i < g_config.max_connections; ++i) {
        sle_server_connection_t server;
        if (!server_connections_get_server_copy(&g_server_connections, i, &server)) {
            continue;
        }
        
        /* 跳过未使用、空闲、断开状态 */
        if (!server.used || server.state == SLE_SERVER_IDLE || 
            server.state == SLE_SERVER_DISCONNECTED) {
            continue;
        }
        
        /* 检查connecting状态超时 */
        if (server.state == SLE_SERVER_CONNECTING) {
            if (g_config.early_connect_abort_ms > 0 && server.state_enter_ms > 0 &&
                now - server.state_enter_ms >= g_config.early_connect_abort_ms) {
                char addr[32] = {0};
                server_connections_addr_to_string(&server.addr, addr, sizeof(addr));
                fprintf(stderr, "[SLE][WARN] early connect abort server_index=%u addr=%s elapsed_ms=%llu\n",
                    i, addr, (unsigned long long)(now - server.state_enter_ms));
                (void)sle_disconnect_remote_device(&server.addr);
                candidate_mark_failed(&server.addr, now);
                server_connections_mark_disconnected(&g_server_connections, i,
                    SLE_DISCONNECT_REASON_CONNECTING_TIMEOUT);
                g_has_pending_connect = false;
                g_pending_connect_index = -1;
                print_connection_table();
                candidate_try_start_best();
                need_scan_restart = true;
                continue;
            }
            if (server_connections_is_state_timeout(&g_server_connections, i, now)) {
                char addr[32] = {0};
                server_connections_addr_to_string(&server.addr, addr, sizeof(addr));
                fprintf(stderr, "[SLE][WARN] connecting timeout server_index=%u addr=%s\n", i, addr);
                (void)sle_disconnect_remote_device(&server.addr);
                candidate_mark_failed(&server.addr, now);
                server_connections_mark_disconnected(&g_server_connections, i, 
                    SLE_DISCONNECT_REASON_CONNECTING_TIMEOUT);
                g_has_pending_connect = false;
                g_pending_connect_index = -1;
                print_connection_table();
                candidate_try_start_best();
                need_scan_restart = true;
                continue;
            }
        }
        
        /* 检查pairing状态超时 */
        if (server.state == SLE_SERVER_PAIRING) {
            if (server_connections_is_state_timeout(&g_server_connections, i, now)) {
                char addr[32] = {0};
                server_connections_addr_to_string(&server.addr, addr, sizeof(addr));
                fprintf(stderr, "[SLE][WARN] pairing timeout server_index=%u addr=%s\n", i, addr);
                sle_remove_paired_remote_device(&server.addr);
                server_connections_mark_disconnected(&g_server_connections, i, 
                    SLE_DISCONNECT_REASON_PAIRING_TIMEOUT);
                print_connection_table();
                need_scan_restart = true;
                continue;
            }
        }
        
        /* 检查discovering状态超时 */
        if (server.state == SLE_SERVER_DISCOVERING) {
            if (server_connections_is_state_timeout(&g_server_connections, i, now)) {
                char addr[32] = {0};
                server_connections_addr_to_string(&server.addr, addr, sizeof(addr));
                fprintf(stderr, "[SLE][WARN] discovery timeout server_index=%u addr=%s\n", i, addr);
                (void)sle_disconnect_remote_device(&server.addr);
                server_connections_mark_disconnected(&g_server_connections, i, 
                    SLE_DISCONNECT_REASON_DISCOVERY_TIMEOUT);
                print_connection_table();
                need_scan_restart = true;
                continue;
            }
        }

        /* 已连接但迟迟没有进入 MTU/discovery，多数是 exchange_info 回调丢失或链路异常。 */
        if (server.state == SLE_SERVER_CONNECTED && !server.mtu_done) {
            if (server_connections_is_state_timeout(&g_server_connections, i, now)) {
                char addr[32] = {0};
                server_connections_addr_to_string(&server.addr, addr, sizeof(addr));
                fprintf(stderr, "[SLE][WARN] connected exchange timeout server_index=%u addr=%s\n", i, addr);
                (void)sle_disconnect_remote_device(&server.addr);
                server_connections_mark_disconnected(&g_server_connections, i,
                    SLE_DISCONNECT_REASON_DISCOVERY_TIMEOUT);
                print_connection_table();
                need_scan_restart = true;
                continue;
            }
        }
        
        /* 参数更新只在 READY 后等待；CONNECTED 还没完成发现，不能按参数更新放行。 */
        if (server.state == SLE_SERVER_READY && !server.param_update_done) {
            if (g_config.param_update_timeout_ms > 0 &&
                server_connections_is_state_timeout(&g_server_connections, i, now)) {
                fprintf(stderr, "[SLE][WARN] param update timeout server_index=%u, continue scan\n", i);
                server_connections_set_param_update_done(&g_server_connections, i, true);
                server_connections_set_state_timeout(&g_server_connections, i, 0);
                if (has_active_capacity()) {
                    need_scan_restart = true;
                }
            }
        }

        /* 检查空闲连接超时（stale检测） */
        if (server.state == SLE_SERVER_READY) {
            uint64_t last_seen = server.last_rx_ms > server.last_state_ms ? 
                server.last_rx_ms : server.last_state_ms;
            if (last_seen > 0 && g_config.stale_timeout_ms > 0 && 
                now - last_seen >= g_config.stale_timeout_ms) {
                char addr[32] = {0};
                server_connections_addr_to_string(&server.addr, addr, sizeof(addr));
                fprintf(stderr, "[SLE][WARN] stale connection server_index=%u addr=%s idle_ms=%llu\n",
                    i, addr, (unsigned long long)(now - last_seen));
                mark_server_stale_disconnected(i, &server);
                need_scan_restart = true;
                continue;
            }
        }

    }
    
    /* 超时释放后触发扫描重启 */
    if (need_scan_restart) {
        request_scan_restart();
    }
}

bool sle_manager_is_running(void)
{
    return g_running;
}
