#ifndef DTU_TYPES_H
#define DTU_TYPES_H

/* DTU 共享类型头：
 * 1. 放 mode / channel / runtime config 等共享结构
 * 2. 不放实现逻辑
 * 3. 供 protocol / storage / mode / channel 共同复用
 */

#include <stdint.h>

#include "dtu_build_config.h"
#include "wifi_device.h"

typedef enum {
    DTU_MODE_CONFIG = 0x00,
    DTU_MODE_RUN = 0x01
} dtu_mode_t;

typedef enum {
    DTU_TRANSPORT_UART = 0x00,
    DTU_TRANSPORT_BLE = 0x01,
    DTU_TRANSPORT_SLE = 0x02,
    DTU_TRANSPORT_MAX
} dtu_transport_id_t;

typedef enum {
    DTU_RX_PROFILE_FAST_RESPONSE = 0x00,
    DTU_RX_PROFILE_BATCH = 0x01
} dtu_rx_profile_t;

typedef struct {
    uint8_t baud_level;
    uint8_t parity;
    uint8_t stop_bits;
    uint8_t data_bits;
} dtu_uart_cfg_t;

typedef struct {
    uint8_t addr;
    uint8_t dev_type;
} dtu_modbus_item_t;

typedef struct {
    uint8_t mac[WIFI_MAC_LEN];
    /* node 子配置：
     * 1. 仅在 ROOT 角色维护白名单时使用
     * 2. power 不在这里单独保存，node 统一跟随 ROOT 当前 power
     */
    dtu_uart_cfg_t uart_cfg;
    uint8_t modbus_count;
    dtu_modbus_item_t modbus[DTU_CFG_MAX_MODBUS_ITEMS];
} dtu_wl_item_t;

typedef struct {
    uint8_t role;
    dtu_uart_cfg_t uart_cfg;
    uint8_t modbus_count;
    dtu_modbus_item_t modbus[DTU_CFG_MAX_MODBUS_ITEMS];
    uint8_t power;
    uint8_t wl_count;
    dtu_wl_item_t whitelist[DTU_CFG_MAX_WL_ITEMS];
} dtu_runtime_cfg_t;

#endif
