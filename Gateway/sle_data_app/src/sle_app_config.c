#include "sle_app_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size > 64 * 1024) {
        fclose(fp);
        return NULL;
    }
    char *buf = (char *)calloc((size_t)size + 1, 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    return buf;
}

/* 第一版配置文件字段很少，使用轻量解析，避免为独立测试 APP 引入 JSON 依赖。 */
static void load_uint16(const char *text, const char *key, uint16_t *value)
{
    const char *p = strstr(text, key);
    if (p == NULL) {
        return;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return;
    }
    unsigned long parsed = strtoul(p + 1, NULL, 0);
    if (parsed <= 0xFFFF) {
        *value = (uint16_t)parsed;
    }
}

static void load_uint32(const char *text, const char *key, uint32_t *value)
{
    const char *p = strstr(text, key);
    if (p == NULL) {
        return;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return;
    }
    unsigned long parsed = strtoul(p + 1, NULL, 0);
    if (parsed <= 0xFFFFFFFFUL) {
        *value = (uint32_t)parsed;
    }
}

static void load_uint8(const char *text, const char *key, uint8_t *value)
{
    uint16_t tmp = *value;
    load_uint16(text, key, &tmp);
    if (tmp <= SLE_DATA_APP_MAX_CONNECTIONS) {
        *value = (uint8_t)tmp;
    }
}

static void load_bool(const char *text, const char *key, bool *value)
{
    const char *p = strstr(text, key);
    if (p == NULL) {
        return;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return;
    }
    while (*p == ':' || *p == ' ' || *p == '\t') {
        p++;
    }
    if (strncmp(p, "true", 4) == 0) {
        *value = true;
    } else if (strncmp(p, "false", 5) == 0) {
        *value = false;
    }
}

static void load_string(const char *text, const char *key, char *value, size_t value_len)
{
    const char *p = strstr(text, key);
    if (p == NULL || value == NULL || value_len == 0) {
        return;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return;
    }
    p = strchr(p, '"');
    if (p == NULL) {
        return;
    }
    p++;
    const char *end = strchr(p, '"');
    if (end == NULL || end <= p) {
        return;
    }
    size_t len = (size_t)(end - p);
    if (len >= value_len) {
        len = value_len - 1;
    }
    memcpy(value, p, len);
    value[len] = '\0';
}

static void load_hex_u16_string(const char *text, const char *key, uint16_t *value)
{
    char buf[16] = {0};
    load_string(text, key, buf, sizeof(buf));
    if (buf[0] == '\0') {
        return;
    }
    unsigned long parsed = strtoul(buf, NULL, 0);
    if (parsed <= 0xFFFF) {
        *value = (uint16_t)parsed;
    }
}

void sle_app_config_init_defaults(sle_app_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->max_connections = SLE_APP_DEFAULT_MAX_CONNECTIONS;
    config->target_active_connections = SLE_APP_DEFAULT_TARGET_ACTIVE_CONNECTIONS;
    config->scan_interval = SLE_APP_DEFAULT_SCAN_INTERVAL;
    config->scan_window = SLE_APP_DEFAULT_SCAN_WINDOW;
    config->conn_scan_interval = SLE_APP_DEFAULT_CONN_SCAN_INTERVAL;
    config->conn_scan_window = SLE_APP_DEFAULT_CONN_SCAN_WINDOW;
    config->conn_interval = SLE_APP_DEFAULT_CONN_INTERVAL;
    config->conn_latency = SLE_APP_DEFAULT_CONN_LATENCY;
    config->supervision_timeout = SLE_APP_DEFAULT_SUPERVISION_TIMEOUT;
    config->mtu = SLE_APP_DEFAULT_MTU;
    config->mac_prefix = SLE_APP_DEFAULT_MAC_PREFIX;
    config->data_property_uuid = SLE_APP_DEFAULT_DATA_PROPERTY_UUID;
    config->auto_select_data_property = SLE_APP_DEFAULT_AUTO_SELECT_PROPERTY;
    config->fallback_name_filter_enabled = SLE_APP_DEFAULT_FALLBACK_NAME_ENABLED;
    snprintf(config->fallback_target_name, sizeof(config->fallback_target_name),
        SLE_APP_DEFAULT_FALLBACK_TARGET_NAME);
    snprintf(config->persistence_path, sizeof(config->persistence_path),
        SLE_APP_DEFAULT_PERSISTENCE_PATH);
    config->continue_scan_when_full = SLE_APP_DEFAULT_CONTINUE_SCAN_WHEN_FULL;
    config->enable_mcs = SLE_APP_DEFAULT_ENABLE_MCS;
    config->enable_phy_update = SLE_APP_DEFAULT_ENABLE_PHY_UPDATE;
    config->enable_large_data_len = SLE_APP_DEFAULT_ENABLE_LARGE_DATA_LEN;
    config->stale_timeout_ms = SLE_APP_DEFAULT_STALE_TIMEOUT_MS;
    
    /* 连接流程超时和重试控制默认值 */
    config->connecting_timeout_ms = SLE_APP_DEFAULT_CONNECTING_TIMEOUT_MS;
    config->pairing_timeout_ms = SLE_APP_DEFAULT_PAIRING_TIMEOUT_MS;
    config->discovery_timeout_ms = SLE_APP_DEFAULT_DISCOVERY_TIMEOUT_MS;
    config->param_update_timeout_ms = SLE_APP_DEFAULT_PARAM_UPDATE_TIMEOUT_MS;
    config->scan_restart_delay_ms = SLE_APP_DEFAULT_SCAN_RESTART_DELAY_MS;
    config->connect_retry_backoff_ms = SLE_APP_DEFAULT_CONNECT_RETRY_BACKOFF_MS;
    config->connect_fail_penalty_ms = SLE_APP_DEFAULT_CONNECT_FAIL_PENALTY_MS;
    config->early_connect_abort_ms = SLE_APP_DEFAULT_EARLY_CONNECT_ABORT_MS;
    config->wait_param_update_before_scan = SLE_APP_DEFAULT_WAIT_PARAM_UPDATE;
    config->enable_verbose_log = SLE_APP_DEFAULT_ENABLE_VERBOSE_LOG;
}

int sle_app_config_load(const char *path, sle_app_config_t *config)
{
    if (path == NULL || config == NULL) {
        return -1;
    }
    char *text = read_file(path);
    if (text == NULL) {
        printf("[SLE][WARN] config not found or unreadable, use defaults: %s\n", path);
        return -1;
    }
    load_uint8(text, "\"max_connections\"", &config->max_connections);
    load_uint8(text, "\"target_active_connections\"", &config->target_active_connections);
    load_uint16(text, "\"scan_interval\"", &config->scan_interval);
    load_uint16(text, "\"scan_window\"", &config->scan_window);
    load_uint16(text, "\"conn_scan_interval\"", &config->conn_scan_interval);
    load_uint16(text, "\"conn_scan_window\"", &config->conn_scan_window);
    load_uint16(text, "\"conn_interval\"", &config->conn_interval);
    load_uint16(text, "\"conn_latency\"", &config->conn_latency);
    load_uint16(text, "\"supervision_timeout\"", &config->supervision_timeout);
    load_uint16(text, "\"mtu\"", &config->mtu);
    load_hex_u16_string(text, "\"mac_prefix\"", &config->mac_prefix);
    load_hex_u16_string(text, "\"data_property_uuid\"", &config->data_property_uuid);
    load_bool(text, "\"auto_select_data_property\"", &config->auto_select_data_property);
    load_bool(text, "\"fallback_name_filter_enabled\"", &config->fallback_name_filter_enabled);
    load_string(text, "\"fallback_target_name\"", config->fallback_target_name, sizeof(config->fallback_target_name));
    load_string(text, "\"persistence_path\"", config->persistence_path, sizeof(config->persistence_path));
    load_bool(text, "\"continue_scan_when_full\"", &config->continue_scan_when_full);
    load_bool(text, "\"enable_mcs\"", &config->enable_mcs);
    load_bool(text, "\"enable_phy_update\"", &config->enable_phy_update);
    load_bool(text, "\"enable_large_data_len\"", &config->enable_large_data_len);
    load_uint32(text, "\"stale_timeout_ms\"", &config->stale_timeout_ms);
    
    /* 连接流程超时和重试控制配置解析 */
    load_uint32(text, "\"connecting_timeout_ms\"", &config->connecting_timeout_ms);
    load_uint32(text, "\"pairing_timeout_ms\"", &config->pairing_timeout_ms);
    load_uint32(text, "\"discovery_timeout_ms\"", &config->discovery_timeout_ms);
    load_uint32(text, "\"param_update_timeout_ms\"", &config->param_update_timeout_ms);
    load_uint32(text, "\"scan_restart_delay_ms\"", &config->scan_restart_delay_ms);
    load_uint32(text, "\"connect_retry_backoff_ms\"", &config->connect_retry_backoff_ms);
    load_uint32(text, "\"connect_fail_penalty_ms\"", &config->connect_fail_penalty_ms);
    load_uint32(text, "\"early_connect_abort_ms\"", &config->early_connect_abort_ms);
    load_bool(text, "\"wait_param_update_before_scan\"", &config->wait_param_update_before_scan);
    load_bool(text, "\"enable_verbose_log\"", &config->enable_verbose_log);
    
    free(text);
    return 0;
}

void sle_app_config_print(const sle_app_config_t *config)
{
    if (config == NULL) {
        return;
    }
    printf("[SLE][CONFIG] max_connections=%u target_active=%u scan=%u/%u conn_scan=%u/%u conn_interval=0x%x conn_latency=0x%x timeout=0x%x mtu=%u mac_prefix=0x%04x data_property_uuid=0x%04x persistence=%s\n",
        config->max_connections,
        config->target_active_connections,
        config->scan_interval,
        config->scan_window,
        config->conn_scan_interval,
        config->conn_scan_window,
        config->conn_interval,
        config->conn_latency,
        config->supervision_timeout,
        config->mtu,
        config->mac_prefix,
        config->data_property_uuid,
        config->persistence_path);
    printf("[SLE][CONFIG] auto_select_data_property=%s fallback_name_filter_enabled=%s fallback_target_name=%s continue_scan_when_full=%s enable_mcs=%s enable_phy_update=%s enable_large_data_len=%s\n",
        config->auto_select_data_property ? "true" : "false",
        config->fallback_name_filter_enabled ? "true" : "false",
        config->fallback_target_name,
        config->continue_scan_when_full ? "true" : "false",
        config->enable_mcs ? "true" : "false",
        config->enable_phy_update ? "true" : "false",
        config->enable_large_data_len ? "true" : "false");
    printf("[SLE][CONFIG] stale_timeout_ms=%u\n", config->stale_timeout_ms);
    printf("[SLE][CONFIG] connecting_timeout_ms=%u pairing_timeout_ms=%u discovery_timeout_ms=%u param_update_timeout_ms=%u\n",
        config->connecting_timeout_ms,
        config->pairing_timeout_ms,
        config->discovery_timeout_ms,
        config->param_update_timeout_ms);
    printf("[SLE][CONFIG] scan_restart_delay_ms=%u connect_retry_backoff_ms=%u connect_fail_penalty_ms=%u early_connect_abort_ms=%u wait_param_update_before_scan=%s enable_verbose_log=%s\n",
        config->scan_restart_delay_ms,
        config->connect_retry_backoff_ms,
        config->connect_fail_penalty_ms,
        config->early_connect_abort_ms,
        config->wait_param_update_before_scan ? "true" : "false",
        config->enable_verbose_log ? "true" : "false");
}

int sle_app_config_validate(const sle_app_config_t *config)
{
    if (config == NULL) {
        return -1;
    }
    
    /* 连接数限制 */
    if (config->max_connections == 0 || config->max_connections > SLE_DATA_APP_MAX_CONNECTIONS) {
        fprintf(stderr, "[CONFIG][ERROR] invalid max_connections: %u (1-%d)\n",
            config->max_connections, SLE_DATA_APP_MAX_CONNECTIONS);
        return -2;
    }
    
    /* 扫描参数 */
    if (config->scan_interval == 0 || config->scan_window == 0) {
        fprintf(stderr, "[CONFIG][ERROR] scan_interval and scan_window must be > 0\n");
        return -3;
    }
    if (config->scan_window > config->scan_interval) {
        fprintf(stderr, "[CONFIG][ERROR] scan_window (%u) > scan_interval (%u)\n",
            config->scan_window, config->scan_interval);
        return -4;
    }
    if (config->conn_scan_interval == 0 || config->conn_scan_window == 0) {
        fprintf(stderr, "[CONFIG][ERROR] conn_scan_interval and conn_scan_window must be > 0\n");
        return -5;
    }
    if (config->conn_scan_window > config->conn_scan_interval) {
        fprintf(stderr, "[CONFIG][ERROR] conn_scan_window (%u) > conn_scan_interval (%u)\n",
            config->conn_scan_window, config->conn_scan_interval);
        return -6;
    }
    
    /* 连接参数 */
    if (config->conn_interval == 0) {
        fprintf(stderr, "[CONFIG][ERROR] conn_interval must be > 0\n");
        return -7;
    }
    if (config->supervision_timeout == 0) {
        fprintf(stderr, "[CONFIG][ERROR] supervision_timeout must be > 0\n");
        return -8;
    }
    
    /* 超时参数合理性检查 */
    if (config->connecting_timeout_ms > 0 && config->connecting_timeout_ms < 1000) {
        fprintf(stderr, "[CONFIG][WARN] connecting_timeout_ms too small: %u (recommend >= 1000)\n",
            config->connecting_timeout_ms);
    }
    if (config->pairing_timeout_ms > 0 && config->pairing_timeout_ms < 1000) {
        fprintf(stderr, "[CONFIG][WARN] pairing_timeout_ms too small: %u (recommend >= 1000)\n",
            config->pairing_timeout_ms);
    }
    if (config->discovery_timeout_ms > 0 && config->discovery_timeout_ms < 1000) {
        fprintf(stderr, "[CONFIG][WARN] discovery_timeout_ms too small: %u (recommend >= 1000)\n",
            config->discovery_timeout_ms);
    }
    
    return 0;
}
