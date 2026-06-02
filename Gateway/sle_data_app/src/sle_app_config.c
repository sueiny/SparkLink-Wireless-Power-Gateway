#include "sle_app_config.h"
#include <stdio.h>
#include <string.h>

/* 所有默认值集中定义于此，修改配置只需改这一处。 */
static const sle_app_config_t kDefaultConfig = {
    /* 连接数量 */
    .max_connections            = 8,
    .target_active_connections  = 3,

    /* 扫描参数 */
    .scan_interval              = 100,
    .scan_window                = 100,

    /* 连接参数 */
    .conn_scan_interval         = 100,
    .conn_scan_window           = 100,
    .conn_interval              = 0x14,
    .conn_latency               = 0x1F3,
    .supervision_timeout        = 0x1F4,
    .mtu                        = 300,

    /* 设备过滤 */
    .mac_prefix                 = 0x12,
    .data_property_uuid         = 0xFDF1,
    .auto_select_data_property  = true,

    /* 存储路径 */
    .persistence_path           = "/userdata/gateway/data/sle",

    /* 扫描策略 */
    .continue_scan_when_full    = false,

    /* 空闲连接保护 */
    .stale_timeout_ms           = 0,

    /* 连接流程超时和重试控制 */
    .connecting_timeout_ms      = 10000,
    .pairing_timeout_ms         = 8000,
    .discovery_timeout_ms       = 8000,
    .param_update_timeout_ms    = 5000,
    .scan_restart_delay_ms      = 500,
    .connect_retry_backoff_ms   = 8000,
    .connect_fail_penalty_ms    = 3000,
    .early_connect_abort_ms     = 0,
    .wait_param_update_before_scan = true,

    /* 调试选项 */
    .enable_verbose_log         = false,
};

void sle_app_config_init_defaults(sle_app_config_t *config)
{
    if (config == NULL) {
        return;
    }
    *config = kDefaultConfig;
}

void sle_app_config_print(const sle_app_config_t *config)
{
    if (config == NULL) {
        return;
    }
    fprintf(stderr, "[SLE][CONFIG] max_connections=%u target_active=%u "
        "scan=%u/%u conn_scan=%u/%u conn_interval=0x%x conn_latency=0x%x "
        "timeout=0x%x mtu=%u mac_prefix=0x%04x data_property_uuid=0x%04x\n",
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
        config->data_property_uuid);
    fprintf(stderr, "[SLE][CONFIG] auto_select_data_property=%s "
        "continue_scan_when_full=%s stale_timeout_ms=%u\n",
        config->auto_select_data_property ? "true" : "false",
        config->continue_scan_when_full ? "true" : "false",
        config->stale_timeout_ms);
    fprintf(stderr, "[SLE][CONFIG] connecting_timeout_ms=%u pairing_timeout_ms=%u "
        "discovery_timeout_ms=%u param_update_timeout_ms=%u\n",
        config->connecting_timeout_ms,
        config->pairing_timeout_ms,
        config->discovery_timeout_ms,
        config->param_update_timeout_ms);
    fprintf(stderr, "[SLE][CONFIG] scan_restart_delay_ms=%u connect_retry_backoff_ms=%u "
        "connect_fail_penalty_ms=%u early_connect_abort_ms=%u "
        "wait_param_update_before_scan=%s enable_verbose_log=%s\n",
        config->scan_restart_delay_ms,
        config->connect_retry_backoff_ms,
        config->connect_fail_penalty_ms,
        config->early_connect_abort_ms,
        config->wait_param_update_before_scan ? "true" : "false",
        config->enable_verbose_log ? "true" : "false");
}
