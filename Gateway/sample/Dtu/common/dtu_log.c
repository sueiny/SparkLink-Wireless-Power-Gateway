/**
 * @file dtu_log.c
 * @brief DTU 统一日志出口。
 */

#include "dtu_log.h"

#include <stdarg.h>

#include "dtu_storage.h"
#include "osal_debug.h"
#include "securec.h"

#define DTU_LOG_PREFIX "[DTU LOG]"

static void dtu_log_print_mac(const uint8_t *mac)
{
    osal_printk("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void dtu_log_printf(const char *fmt, ...)
{
    char buf[256] = {0};
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (ret < 0) {
        osal_printk("%s <format failed>\r\n", DTU_LOG_PREFIX);
        return;
    }
    osal_printk("%s %s\r\n", DTU_LOG_PREFIX, buf);
}

const char *dtu_log_cmd_name(uint8_t cmd)
{
    switch (cmd) {
        case DTU_CFG_CMD_READ_DEV_INFO:
            return "READ_DEV_INFO";
        case DTU_CFG_CMD_READ_UART_CFG:
            return "READ_UART_CFG";
        case DTU_CFG_CMD_READ_MODBUS_CFG:
            return "READ_MODBUS_CFG";
        case DTU_CFG_CMD_READ_ROOT_WL_ALL:
            return "READ_ROOT_WL_ALL";
        case DTU_CFG_CMD_READ_ROOT_POWER:
            return "READ_ROOT_POWER";
        case DTU_CFG_CMD_GET_MODE_STATUS:
            return "GET_MODE_STATUS";
        case DTU_CFG_CMD_READ_WL_NODE_CFG:
            return "READ_WL_NODE_CFG";
        case DTU_CFG_CMD_SET_ROLE:
            return "SET_ROLE";
        case DTU_CFG_CMD_SET_UART_CFG:
            return "SET_UART_CFG";
        case DTU_CFG_CMD_SET_MODBUS_CFG:
            return "SET_MODBUS_CFG";
        case DTU_CFG_CMD_SET_ROOT_POWER:
            return "SET_ROOT_POWER";
        case DTU_CFG_CMD_ADD_WL_ITEM:
            return "ADD_WL_ITEM";
        case DTU_CFG_CMD_DEL_WL_ITEM:
            return "DEL_WL_ITEM";
        case DTU_CFG_CMD_CLEAR_WL:
            return "CLEAR_WL";
        case DTU_CFG_CMD_SET_WL_NODE_CFG:
            return "SET_WL_NODE_CFG";
        case DTU_CFG_CMD_COMMIT:
            return "COMMIT";
        case DTU_CFG_CMD_REBOOT:
            return "REBOOT";
        case DTU_CFG_CMD_FACTORY_RESET:
            return "FACTORY_RESET";
        default:
            return "UNKNOWN_CMD";
    }
}

const char *dtu_log_transport_name(dtu_transport_id_t transport_id)
{
    switch (transport_id) {
        case DTU_TRANSPORT_UART:
            return "UART";
        case DTU_TRANSPORT_BLE:
            return "BLE";
        case DTU_TRANSPORT_SLE:
            return "SLE";
        default:
            return "UNKNOWN";
    }
}

void dtu_log_info(const char *fmt, ...)
{
    char buf[256] = {0};
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (ret < 0) {
        osal_printk("%s <format failed>\r\n", DTU_LOG_PREFIX);
        return;
    }
    osal_printk("%s %s\r\n", DTU_LOG_PREFIX, buf);
}

void dtu_log_transport(const char *transport, const char *fmt, ...)
{
    char buf[192] = {0};
    va_list args;
    int ret;

    if (transport == NULL || fmt == NULL) {
        return;
    }

    va_start(args, fmt);
    ret = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (ret < 0) {
        osal_printk("%s %s: <format failed>\r\n", DTU_LOG_PREFIX, transport);
        return;
    }
    osal_printk("%s %s: %s\r\n", DTU_LOG_PREFIX, transport, buf);
}

static void dtu_log_print_uart_cfg(const char *prefix, const dtu_uart_cfg_t *cfg)
{
    dtu_log_printf("%s uart: baud=%u parity=%s stop=%u data=%u",
        prefix, dtu_storage_uart_baudrate(cfg->baud_level), dtu_storage_parity_name(cfg->parity),
        cfg->stop_bits, cfg->data_bits);
}

static void dtu_log_print_modbus_cfg(const char *prefix, const dtu_runtime_cfg_t *cfg)
{
    dtu_log_printf("%s modbus_count=%u", prefix, cfg->modbus_count);
    for (uint8_t i = 0; i < cfg->modbus_count; i++) {
        dtu_log_printf("%s modbus[%u]: addr=%u dev_type=0x%02X",
            prefix, i, cfg->modbus[i].addr, cfg->modbus[i].dev_type);
    }
}

/* 白名单最多 128 条，启动/提交快照只打印数量，避免刷满串口。 */
static void dtu_log_print_whitelist_summary(const char *prefix, const dtu_runtime_cfg_t *cfg)
{
    dtu_log_printf("%s whitelist_count=%u", prefix, cfg->wl_count);
}

/* boot / commit / factory reset 后打印一份配置快照。 */
static void dtu_log_runtime_snapshot(const char *prefix)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t mac[WIFI_MAC_LEN] = {0};
    uint8_t name[DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t name_len;

    dtu_storage_get_device_mac(mac);
    name_len = dtu_storage_get_device_name(name, sizeof(name));

    dtu_log_printf("%s begin", prefix);
    dtu_log_printf("%s mode: current=%s source=DIP pin=%u level=%s rx_profile=%s",
        prefix,
        dtu_storage_mode_name(dtu_storage_current_mode()),
        (uint32_t)DTU_CFG_MODE_SWITCH_PIN,
        (dtu_storage_current_mode() == DTU_MODE_RUN) ? "HIGH" : "LOW",
        dtu_storage_rx_profile_name(dtu_storage_rx_profile()));
    osal_printk("%s %s role=%s mac=", DTU_LOG_PREFIX, prefix, dtu_storage_role_name(cfg->role));
    dtu_log_print_mac(mac);
    osal_printk(" name=%.*s\r\n", name_len, (const char *)name);
    dtu_log_print_uart_cfg(prefix, &cfg->uart_cfg);
    dtu_log_print_modbus_cfg(prefix, cfg);
    dtu_log_printf("%s power=%u", prefix, cfg->power);
    dtu_log_print_whitelist_summary(prefix, cfg);
    dtu_log_printf("%s end", prefix);
}

void dtu_log_error(const char *fmt, ...)
{
    char buf[192] = {0};
    va_list args;
    int ret;

    va_start(args, fmt);
    ret = vsnprintf_s(buf, sizeof(buf), sizeof(buf) - 1, fmt, args);
    va_end(args);

    if (ret < 0) {
        osal_printk("%s error: <format failed>\r\n", DTU_LOG_PREFIX);
        return;
    }
    osal_printk("%s error: %s\r\n", DTU_LOG_PREFIX, buf);
}

void dtu_log_boot(errcode_t load_ret)
{
    dtu_log_printf("DTU cfg load ret=0x%x", load_ret);
    dtu_log_runtime_snapshot("DTU boot config");
}

void dtu_log_commit(void)
{
    dtu_log_runtime_snapshot("DTU commit config");
}

void dtu_log_factory_reset(void)
{
    dtu_log_runtime_snapshot("DTU factory config");
}

void dtu_log_cfg_read_dev_info(void)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t name[DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t name_len = dtu_storage_get_device_name(name, sizeof(name));

    dtu_log_printf("DTU cfg read dev_info: role=%s name=%.*s",
        dtu_storage_role_name(cfg->role), name_len, (const char *)name);
}

void dtu_log_cfg_read_uart(const dtu_uart_cfg_t *cfg)
{
    dtu_log_print_uart_cfg("DTU cfg read", cfg);
}

void dtu_log_cfg_read_modbus(void)
{
    dtu_log_print_modbus_cfg("DTU cfg read", dtu_storage_runtime_const());
}

void dtu_log_cfg_read_whitelist(void)
{
    dtu_log_print_whitelist_summary("DTU cfg read", dtu_storage_runtime_const());
}

void dtu_log_cfg_read_power(uint8_t power)
{
    dtu_log_printf("DTU cfg read power=%u", power);
}

void dtu_log_cfg_read_wl_node(const dtu_wl_item_t *item)
{
    if (item == NULL) {
        return;
    }
    osal_printk("%s DTU cfg read wl_node: mac=", DTU_LOG_PREFIX);
    dtu_log_print_mac(item->mac);
    osal_printk("\r\n");
    dtu_log_print_uart_cfg("DTU cfg read wl_node", &item->uart_cfg);
    dtu_log_printf("DTU cfg read wl_node modbus_count=%u", item->modbus_count);
    for (uint8_t i = 0; i < item->modbus_count; i++) {
        dtu_log_printf("DTU cfg read wl_node modbus[%u]: addr=%u dev_type=0x%02X",
            i, item->modbus[i].addr, item->modbus[i].dev_type);
    }
}

void dtu_log_cfg_write_role(uint8_t role)
{
    dtu_log_printf("DTU cfg set role=%s", dtu_storage_role_name(role));
}

void dtu_log_cfg_write_uart(const dtu_uart_cfg_t *cfg)
{
    dtu_log_print_uart_cfg("DTU cfg set", cfg);
}

void dtu_log_cfg_write_modbus(void)
{
    dtu_log_print_modbus_cfg("DTU cfg set", dtu_storage_runtime_const());
}
  
void dtu_log_cfg_write_power(uint8_t power)
{
    dtu_log_printf("DTU cfg set power=%u", power);
}

void dtu_log_cfg_write_whitelist(void)
{
    dtu_log_print_whitelist_summary("DTU cfg set", dtu_storage_runtime_const());
}

void dtu_log_cfg_write_wl_node(const dtu_wl_item_t *item)
{
    if (item == NULL) {
        return;
    }
    osal_printk("%s DTU cfg set wl_node: mac=", DTU_LOG_PREFIX);
    dtu_log_print_mac(item->mac);
    osal_printk("\r\n");
    dtu_log_print_uart_cfg("DTU cfg set wl_node", &item->uart_cfg);
    dtu_log_printf("DTU cfg set wl_node modbus_count=%u", item->modbus_count);
    for (uint8_t i = 0; i < item->modbus_count; i++) {
        dtu_log_printf("DTU cfg set wl_node modbus[%u]: addr=%u dev_type=0x%02X",
            i, item->modbus[i].addr, item->modbus[i].dev_type);
    }
}

void dtu_log_cfg_reject(dtu_transport_id_t transport_id, uint8_t cmd)
{
    dtu_log_printf("DTU reject: transport=%s cmd=%s mode=%s",
        dtu_log_transport_name(transport_id),
        dtu_log_cmd_name(cmd),
        dtu_storage_mode_name(dtu_storage_current_mode()));
}

/* 运行态转发摘要仅 trace 打开时输出。 */
void dtu_log_run_forward(dtu_transport_id_t src, dtu_transport_id_t dst, uint16_t payload_len, uint16_t packet_len)
{
#if (DTU_CFG_LOG_TRACE_ENABLE == 0)
    unused(src);
    unused(dst);
    unused(payload_len);
    unused(packet_len);
#else
    dtu_log_printf("DTU run forward: %s -> %s payload_len=%u packet_len=%u",
        dtu_log_transport_name(src), dtu_log_transport_name(dst), payload_len, packet_len);
#endif
}
