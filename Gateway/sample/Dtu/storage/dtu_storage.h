/**
 * @file dtu_storage.h
 * @brief runtime 配置、模式状态、参数校验和 NV 持久化接口。
 */

#ifndef DTU_STORAGE_H
#define DTU_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"
#include "uart.h"

/**
 * @brief 返回可写 runtime 配置，仅配置命令和 storage 内部流程使用。
 * @warning 修改后需调用 dtu_storage_commit() 才会持久化。
 */
dtu_runtime_cfg_t *dtu_storage_runtime(void);

/** 返回只读 runtime 配置，普通读取优先使用该接口。 */
const dtu_runtime_cfg_t *dtu_storage_runtime_const(void);

/** 返回当前模式；模式来自拨码采样，高电平 RUN，低电平 CONFIG。 */
dtu_mode_t dtu_storage_current_mode(void);

/** REBOOT 待执行时，配置写入会被冻结，真正复位由任务安全点完成。 */
bool dtu_storage_is_reboot_pending(void);

/** 设置当前模式；正常启动路径由 dtu_storage_load() 负责。 */
void dtu_storage_set_current_mode(dtu_mode_t mode);

/** 设置 reboot pending 标志，避免在协议处理栈里直接复位导致回包丢失。 */
void dtu_storage_set_reboot_pending(bool pending);

/** 配置协议入参合法性检查。 */
bool dtu_storage_is_valid_mode(uint8_t mode);
bool dtu_storage_is_valid_role(uint8_t role);
bool dtu_storage_is_valid_uart_cfg(const dtu_uart_cfg_t *cfg);
bool dtu_storage_is_valid_dev_type(uint8_t dev_type);

/** 修改当前缓存中的 UART 配置；COMMIT 后才写入 NV。 */
errcode_t dtu_storage_set_uart_cfg(const dtu_uart_cfg_t *cfg);

/** 在 runtime 白名单中按 MAC 查找条目，未找到返回负数。 */
int32_t dtu_storage_find_wl_item(const uint8_t *mac);

/** 初始化白名单 node 子配置，供 ADD_WL_ITEM 新增条目时使用。 */
void dtu_storage_init_wl_item_cfg(dtu_wl_item_t *item);

/** 返回当前 UART RX profile；CONFIG 低延迟，RUN 批量化。 */
dtu_rx_profile_t dtu_storage_rx_profile(void);
uint16_t dtu_storage_rx_notify_length(void);
uint8_t dtu_storage_rx_int_threshold(void);

/** 协议 baud_level 转真实 baudrate。 */
uint32_t dtu_storage_uart_baudrate(uint8_t baud_level);

/** 将协议 UART 配置转换为 SDK uart_attr_t。 */
void dtu_storage_fill_uart_attr(uart_attr_t *uart_attr, const dtu_uart_cfg_t *cfg);

/** 填充默认 runtime 配置，用于首次烧录、NV 无效和恢复出厂。 */
void dtu_storage_set_default(dtu_runtime_cfg_t *cfg);

/** 获取设备 MAC；策略变化应继续收口在 storage 内部。 */
void dtu_storage_get_device_mac(uint8_t *mac);

/** 获取设备名，优先 Kconfig，未配置时使用默认名。 */
uint8_t dtu_storage_get_device_name(uint8_t *name_buf, uint8_t name_buf_len);

/** 枚举值转字符串，统一给日志使用。 */
const char *dtu_storage_role_name(uint8_t role);
const char *dtu_storage_parity_name(uint8_t parity);
const char *dtu_storage_mode_name(dtu_mode_t mode);
const char *dtu_storage_rx_profile_name(dtu_rx_profile_t profile);

/** 从 NV 加载配置并采样拨码；NV 无效时回退默认配置。 */
errcode_t dtu_storage_load(void);

/** 将当前 runtime 配置写入 NV，白名单按 shard 分片保存。 */
errcode_t dtu_storage_commit(void);

/** 恢复默认配置并写入 NV。 */
errcode_t dtu_storage_factory_reset(void);

#endif
