#ifndef GATEWAY_SLE_APP_CONFIG_H
#define GATEWAY_SLE_APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define SLE_DATA_APP_NAME_MAX 64
#define SLE_DATA_APP_PATH_MAX 160
#define SLE_DATA_APP_MAX_CONNECTIONS 8

/**
 * @brief sle_data_app 配置结构体
 * @note 默认值在 sle_app_config.c 的 kDefaultConfig 中集中定义。
 *       不再支持 JSON 文件加载，修改配置需重新编译。
 */
typedef struct {
    /* 连接数量 */
    uint8_t max_connections;
    uint8_t target_active_connections;

    /* 扫描参数 */
    uint16_t scan_interval;
    uint16_t scan_window;

    /* 连接参数 */
    uint16_t conn_scan_interval;
    uint16_t conn_scan_window;
    uint16_t conn_interval;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
    uint16_t mtu;

    /* 设备过滤 */
    uint16_t mac_prefix;
    uint16_t data_property_uuid;
    bool auto_select_data_property;

    /* 存储路径 */
    char persistence_path[SLE_DATA_APP_PATH_MAX];

    /* 扫描策略 */
    bool continue_scan_when_full;

    /* 空闲连接保护 */
    uint32_t stale_timeout_ms;

    /* 连接流程超时和重试控制 */
    uint32_t connecting_timeout_ms;
    uint32_t pairing_timeout_ms;
    uint32_t discovery_timeout_ms;
    uint32_t param_update_timeout_ms;
    uint32_t scan_restart_delay_ms;
    uint32_t connect_retry_backoff_ms;
    uint32_t connect_fail_penalty_ms;
    uint32_t early_connect_abort_ms;
    bool wait_param_update_before_scan;

    /* 调试选项 */
    bool enable_verbose_log;
} sle_app_config_t;

/* 用编译期默认值初始化配置结构体。 */
void sle_app_config_init_defaults(sle_app_config_t *config);

/* 打印最终生效配置，便于板端对齐 server 参数。 */
void sle_app_config_print(const sle_app_config_t *config);

#endif
