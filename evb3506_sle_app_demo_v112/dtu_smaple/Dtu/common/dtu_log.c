/**
 * @file dtu_log.c
 * @brief DTU统一日志模块实现
 * @details 本模块负责DTU系统的统一日志管理，主要职责：
 *          1. 收敛DTU业务日志，只保留启动、提交、读写配置、reject、错误几类日志
 *          2. 避免mode/storage/channel各处直接散落osal_printk
 *          3. 让日志风格保持一致，后续增减日志点时只需集中修改本文件
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 日志级别说明：
 *       - dtu_log_info：普通信息日志，用于流程跟踪
 *       - dtu_log_error：错误日志，用于异常情况
 *       - dtu_log_transport：传输层日志，用于数据收发跟踪
 *       - dtu_log_*：特定功能日志，用于配置读写、生命周期等
 *
 * @par 日志格式：
 *       - 统一前缀：[DTU LOG]
 *       - 时间戳：由系统提供
 *       - 模块标识：transport名称、命令名称等
 *       - 日志内容：具体信息
 *
 * @note 所有日志函数都使用vsnprintf_s进行格式化，确保缓冲区安全。
 *       日志缓冲区大小为256字节，超过会截断。
 */

#include "dtu_log.h"

#include <stdarg.h>

#include "dtu_service_internal.h"
#include "dtu_storage.h"
#include "osal_debug.h"
#include "securec.h"

/** @brief DTU日志统一前缀 */
#define DTU_LOG_PREFIX "[DTU LOG]"

/* 统一日志模块职责：
 * 1. 收敛 DTU 业务日志，只保留启动、提交、读写配置、reject、错误几类日志
 * 2. 避免 mode / storage / channel 各处直接散落 osal_printk
 * 3. 让日志风格保持一致，后续增减日志点时只需集中修改本文件
 */

/* ========================================================================== */
/* 私有打印辅助区                                                             */
/* @brief 日志内部复用的小工具函数                                            */
/* @details 这些函数不对外导出，避免其它模块直接绕开统一日志接口              */
/* ========================================================================== */

/**
 * @brief 打印MAC地址
 * @details 以AA:BB:CC:DD:EE:FF格式打印MAC地址
 *
 * @param[in] mac MAC地址字节数组，长度必须为WIFI_MAC_LEN(6字节)
 *
 * @note 使用%02X格式确保每个字节都打印两位十六进制数
 *       即使是00也会打印为00
 */
static void dtu_log_print_mac(const uint8_t *mac)
{
    osal_printk("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief 打印统一前缀日志行
 * @details 使用统一的前缀格式打印日志，确保日志风格一致
 *
 * @param[in] fmt 格式化字符串
 * @param[in] ... 可变参数
 *
 * @note 日志缓冲区大小为256字节，超过会截断
 *       格式化失败会打印"<format failed>"错误信息
 */
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

/**
 * @brief 打印普通DTU日志
 * @details 打印DTU系统的一般信息日志，用于流程跟踪
 *
 * @param[in] fmt 格式化字符串
 * @param[in] ... 可变参数
 *
 * @note 该函数是对外接口，供其他模块调用打印信息日志
 */
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

/**
 * @brief 打印transport相关日志
 * @details 打印传输层相关日志，统一归入DTU日志前缀
 *
 * @param[in] transport 传输通道名称（如"UART0"、"BLE"、"SLE"）
 * @param[in] fmt 格式化字符串
 * @param[in] ... 可变参数
 *
 * @note 该函数用于打印数据收发相关的日志
 *       transport参数为NULL时直接返回，不打印任何内容
 */
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

/**
 * @brief 打印一行串口配置摘要
 * @details 打印串口配置的关键参数，便于快速查看配置状态
 *
 * @param[in] prefix 日志前缀字符串
 * @param[in] cfg 串口配置结构体指针
 *
 * @note 打印内容包括：波特率、校验位、停止位、数据位
 */
static void dtu_log_print_uart_cfg(const char *prefix, const dtu_uart_cfg_t *cfg)
{
    dtu_log_printf("%s uart: baud=%u parity=%s stop=%u data=%u",
        prefix, dtu_storage_uart_baudrate(cfg->baud_level), dtu_storage_parity_name(cfg->parity),
        cfg->stop_bits, cfg->data_bits);
}

/**
 * @brief 打印完整Modbus配置表
 * @details 打印所有Modbus配置项，便于查看设备配置
 *
 * @param[in] prefix 日志前缀字符串
 * @param[in] cfg 运行时配置结构体指针
 *
 * @note 打印内容包括：Modbus条目数量、每个条目的地址和设备类型
 */
static void dtu_log_print_modbus_cfg(const char *prefix, const dtu_runtime_cfg_t *cfg)
{
    dtu_log_printf("%s modbus_count=%u", prefix, cfg->modbus_count);
    for (uint8_t i = 0; i < cfg->modbus_count; i++) {
        dtu_log_printf("%s modbus[%u]: addr=%u dev_type=0x%02X",
            prefix, i, cfg->modbus[i].addr, cfg->modbus[i].dev_type);
    }
}

/**
 * @brief 打印白名单摘要
 * @details 打印白名单条目数量，避免每次reboot把所有node子配置刷满串口
 *
 * @param[in] prefix 日志前缀字符串
 * @param[in] cfg 运行时配置结构体指针
 *
 * @note 白名单现在最多128条，V2的READ_ROOT_WL_ALL只关心MAC列表
 *       启动/提交快照只打印数量，避免日志过多
 */
static void dtu_log_print_whitelist_summary(const char *prefix, const dtu_runtime_cfg_t *cfg)
{
    dtu_log_printf("%s whitelist_count=%u", prefix, cfg->wl_count);
}

/**
 * @brief 打印完整运行配置快照
 * @details 打印当前运行配置的所有关键参数，便于一次性看全量状态
 *
 * @param[in] prefix 日志前缀字符串
 *
 * @note 这类日志只在boot/commit/factory reset后使用
 *       打印内容包括：模式、角色、MAC、名称、串口配置、Modbus配置、功率、白名单数量
 */
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
        (dtu_storage_current_mode() == DTU_MODE_CONFIG) ? "HIGH" : "LOW",
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

/* ========================================================================== */
/* 错误日志区                                                                 */
/* 说明：                                                                     */
/* 1. 错误日志统一带 DTU error 前缀。                                          */
/* 2. 这样串口上能快速把错误和普通业务日志区分开。                            */
/* ========================================================================== */

/* 打印统一错误日志。 */
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

/* ========================================================================== */
/* 生命周期日志区                                                             */
/* ========================================================================== */

/* 启动完成后打印当前已加载配置。 */
void dtu_log_boot(errcode_t load_ret)
{
    dtu_log_printf("DTU cfg load ret=0x%x", load_ret);
    dtu_log_runtime_snapshot("DTU boot config");
}

/* COMMIT 成功后打印当前完整配置快照。 */
void dtu_log_commit(void)
{
    dtu_log_runtime_snapshot("DTU commit config");
}

/* 恢复出厂后打印新的完整配置快照。 */
void dtu_log_factory_reset(void)
{
    dtu_log_runtime_snapshot("DTU factory config");
}

/* ========================================================================== */
/* 配置读取日志区                                                             */
/* ========================================================================== */

/* 打印设备信息读取日志。 */
void dtu_log_cfg_read_dev_info(void)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    uint8_t name[DTU_CFG_MAX_NAME_LEN] = {0};
    uint8_t name_len = dtu_storage_get_device_name(name, sizeof(name));

    dtu_log_printf("DTU cfg read dev_info: role=%s name=%.*s",
        dtu_storage_role_name(cfg->role), name_len, (const char *)name);
}

/* 打印串口配置读取日志。 */
void dtu_log_cfg_read_uart(const dtu_uart_cfg_t *cfg)
{
    dtu_log_print_uart_cfg("DTU cfg read", cfg);
}

/* 打印 Modbus 配置读取日志。 */
void dtu_log_cfg_read_modbus(void)
{
    dtu_log_print_modbus_cfg("DTU cfg read", dtu_storage_runtime_const());
}

/* 打印白名单读取日志。 */
void dtu_log_cfg_read_whitelist(void)
{
    dtu_log_print_whitelist_summary("DTU cfg read", dtu_storage_runtime_const());
}

/* 打印 ROOT 功率读取日志。 */
void dtu_log_cfg_read_power(uint8_t power)
{
    dtu_log_printf("DTU cfg read power=%u", power);
}

/* 打印白名单 node 子配置读取日志。 */
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

/* ========================================================================== */
/* 配置写入日志区                                                             */
/* ========================================================================== */

/* 打印角色写入日志。 */
void dtu_log_cfg_write_role(uint8_t role)
{
    dtu_log_printf("DTU cfg set role=%s", dtu_storage_role_name(role));
}

/* 打印串口配置写入日志。 */
void dtu_log_cfg_write_uart(const dtu_uart_cfg_t *cfg)
{
    dtu_log_print_uart_cfg("DTU cfg set", cfg);
}

/* 打印 Modbus 配置写入日志。 */
void dtu_log_cfg_write_modbus(void)
{
    dtu_log_print_modbus_cfg("DTU cfg set", dtu_storage_runtime_const());
}
  
/* 打印 ROOT 功率写入日志。 */
void dtu_log_cfg_write_power(uint8_t power)
{
    dtu_log_printf("DTU cfg set power=%u", power);
}

/* 打印白名单写入日志。 */
void dtu_log_cfg_write_whitelist(void)
{
    dtu_log_print_whitelist_summary("DTU cfg set", dtu_storage_runtime_const());
}

/* 打印白名单 node 子配置写入日志。 */
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

/* ========================================================================== */
/* Reject 日志区                                                              */
/* ========================================================================== */

/* 打印运行模式下的拒配日志。 */
void dtu_log_cfg_reject(dtu_transport_id_t transport_id, uint8_t cmd)
{
    dtu_log_printf("DTU reject: transport=%s cmd=%s mode=%s",
        dtu_service_transport_name(transport_id),
        dtu_service_cmd_name(cmd),
        dtu_storage_mode_name(dtu_storage_current_mode()));
}

/* ========================================================================== */
/* 运行模式日志区                                                             */
/* ========================================================================== */

/* 打印运行态 UART -> SLE 转发摘要，仅 trace 打开时输出。 */
void dtu_log_run_forward(dtu_transport_id_t src, dtu_transport_id_t dst, uint16_t payload_len, uint16_t packet_len)
{
#if (DTU_CFG_LOG_TRACE_ENABLE == 0)
    unused(src);
    unused(dst);
    unused(payload_len);
    unused(packet_len);
#else
    dtu_log_printf("DTU run forward: %s -> %s payload_len=%u packet_len=%u",
        dtu_service_transport_name(src), dtu_service_transport_name(dst), payload_len, packet_len);
#endif
}
