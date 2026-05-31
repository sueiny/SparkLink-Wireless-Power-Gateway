#ifndef GATEWAY_SLE_APP_CONFIG_H
#define GATEWAY_SLE_APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define SLE_DATA_APP_NAME_MAX 64
#define SLE_DATA_APP_PATH_MAX 160
#define SLE_DATA_APP_MAX_CONNECTIONS 8

/* ==========================================================================
 * 常调参数集中区
 *
 * 1. 这里是编译进程序的默认值，适合开发时一眼查看。
 * 2. 板端运行时会再读取 sle_data_app.json 覆盖这些默认值。
 * 3. 优先改 JSON 做现场调试；确认稳定后再同步这里的默认值。
 * ========================================================================== */
#define SLE_APP_DEFAULT_MAX_CONNECTIONS            8
#define SLE_APP_DEFAULT_TARGET_ACTIVE_CONNECTIONS  3
#define SLE_APP_DEFAULT_SCAN_INTERVAL              100
#define SLE_APP_DEFAULT_SCAN_WINDOW                100
#define SLE_APP_DEFAULT_CONN_SCAN_INTERVAL         100
#define SLE_APP_DEFAULT_CONN_SCAN_WINDOW           100
#define SLE_APP_DEFAULT_CONN_INTERVAL              0x14
#define SLE_APP_DEFAULT_CONN_LATENCY               0x1F3
#define SLE_APP_DEFAULT_SUPERVISION_TIMEOUT        0x1F4
#define SLE_APP_DEFAULT_MTU                        300
#define SLE_APP_DEFAULT_MAC_PREFIX                 0x12
#define SLE_APP_DEFAULT_DATA_PROPERTY_UUID         0xFDF1
#define SLE_APP_DEFAULT_AUTO_SELECT_PROPERTY       true
#define SLE_APP_DEFAULT_FALLBACK_NAME_ENABLED      false
#define SLE_APP_DEFAULT_FALLBACK_TARGET_NAME       "DTU_N01"
#define SLE_APP_DEFAULT_PERSISTENCE_PATH           "/userdata/gateway/data/sle"
#define SLE_APP_DEFAULT_CONTINUE_SCAN_WHEN_FULL    false
#define SLE_APP_DEFAULT_ENABLE_MCS                 false
#define SLE_APP_DEFAULT_ENABLE_PHY_UPDATE          false
#define SLE_APP_DEFAULT_ENABLE_LARGE_DATA_LEN      false
#define SLE_APP_DEFAULT_STALE_TIMEOUT_MS           0

#define SLE_APP_DEFAULT_CONNECTING_TIMEOUT_MS      10000   /* connecting 状态超时 */
#define SLE_APP_DEFAULT_PAIRING_TIMEOUT_MS         8000    /* pairing 状态超时 */
#define SLE_APP_DEFAULT_DISCOVERY_TIMEOUT_MS       8000    /* 服务发现超时 */
#define SLE_APP_DEFAULT_PARAM_UPDATE_TIMEOUT_MS    5000    /* 参数更新超时 */
#define SLE_APP_DEFAULT_SCAN_RESTART_DELAY_MS      500     /* 扫描重启延迟 */
#define SLE_APP_DEFAULT_WAIT_PARAM_UPDATE          true    /* READY 后是否等待参数更新完成再扫描 */
#define SLE_APP_DEFAULT_ENABLE_VERBOSE_LOG         false   /* 是否启用详细日志 */
#define SLE_APP_DEFAULT_CONNECT_RETRY_BACKOFF_MS   8000    /* 连接失败后跳过该 MAC 的退避时间 */
#define SLE_APP_DEFAULT_CONNECT_FAIL_PENALTY_MS    3000    /* link create 失败后的候选降权时间 */
#define SLE_APP_DEFAULT_EARLY_CONNECT_ABORT_MS     0       /* 提前中止连接，0 表示关闭 */

/**
 * @brief sle_data_app 配置结构体
 * @note 保持字段少而明确，避免测试 APP 过度设计
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
    bool fallback_name_filter_enabled;
    char fallback_target_name[SLE_DATA_APP_NAME_MAX];
    
    /* 存储路径 */
    char persistence_path[SLE_DATA_APP_PATH_MAX];
    
    /* 扫描策略 */
    bool continue_scan_when_full;
    
    /* PHY配置 */
    bool enable_mcs;
    bool enable_phy_update;
    bool enable_large_data_len;
    
    /* 空闲连接保护 */
    uint32_t stale_timeout_ms;
    
    /* 连接流程超时和重试控制 */
    uint32_t connecting_timeout_ms;         /* connecting 状态超时（毫秒） */
    uint32_t pairing_timeout_ms;            /* pairing 状态超时（毫秒） */
    uint32_t discovery_timeout_ms;          /* 服务发现超时（毫秒） */
    uint32_t param_update_timeout_ms;       /* 参数更新超时（毫秒） */
    uint32_t scan_restart_delay_ms;         /* 扫描重启延迟（毫秒） */
    uint32_t connect_retry_backoff_ms;      /* 单个 MAC 连接失败后的重试退避 */
    uint32_t connect_fail_penalty_ms;       /* 单个 MAC link create 失败后的候选降权 */
    uint32_t early_connect_abort_ms;        /* connecting 提前中止，0 表示关闭 */
    bool wait_param_update_before_scan;     /* 是否等待参数更新完成再扫描 */
    
    /* 调试选项 */
    bool enable_verbose_log;                /* 是否启用详细日志 */
} sle_app_config_t;

/* 写入 WS73 Linux 已验证较稳定的默认参数。 */
void sle_app_config_init_defaults(sle_app_config_t *config);

/* 从 JSON 配置文件加载可调参数；读取失败时调用方继续使用默认值。 */
int sle_app_config_load(const char *path, sle_app_config_t *config);

/* 打印最终生效配置，便于板端对齐 server 参数。 */
void sle_app_config_print(const sle_app_config_t *config);

/**
 * @brief 验证配置参数有效性
 * @param config 配置结构体指针
 * @return 0表示有效，负值表示无效
 */
int sle_app_config_validate(const sle_app_config_t *config);

#endif
