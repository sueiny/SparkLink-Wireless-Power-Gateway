/**
 * @file dtu_storage.h
 * @brief DTU存储中心对外接口
 * @details 本头文件定义了DTU存储中心的对外接口，包括：
 *          - 运行时配置访问接口
 *          - 模式状态管理接口
 *          - 参数校验接口
 *          - 配置修改接口
 *          - NV存储操作接口
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 使用说明：
 *       - 其他模块通过本头文件访问存储中心的功能
 *       - 存储中心负责数据的持久化和一致性
 *       - 不直接访问存储中心的内部数据
 *
 * @par 访问模式：
 *       - 只读访问：使用dtu_storage_runtime_const()获取只读配置
 *       - 可写访问：使用dtu_storage_runtime()获取可写配置（仅配置命令处理使用）
 *       - 受控修改：使用dtu_storage_set_*()函数修改特定参数
 */

#ifndef DTU_STORAGE_H
#define DTU_STORAGE_H

/* storage 头文件：
 * 1. 描述运行配置、模式状态和持久化接口
 * 2. 其他模块只通过这些函数访问状态，不直接持有全局变量
 */

#include <stdbool.h>
#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"
#include "uart.h"

/**
 * @brief 获取可写runtime配置
 * @details 仅配置命令处理和storage内部默认值/加载流程使用
 *          普通读取优先使用const版本
 *
 * @return 可写runtime配置指针
 *
 * @warning 修改配置后需要调用dtu_storage_commit()才能持久化
 */
dtu_runtime_cfg_t *dtu_storage_runtime(void);

/**
 * @brief 获取只读runtime配置快照
 * @details 日志、读取命令、transport初始化都应优先使用这个接口
 *          避免误改状态
 *
 * @return 只读runtime配置指针
 */
const dtu_runtime_cfg_t *dtu_storage_runtime_const(void);

/**
 * @brief 获取当前模式
 * @details 当前模式来自GPIO13拨码采样：高电平CONFIG，低电平RUN
 *
 * @return 当前模式枚举值
 */
dtu_mode_t dtu_storage_current_mode(void);

/**
 * @brief 查询是否已有REBOOT命令待执行
 * @details manager/config会用它拒绝后续配置命令
 *          UART task会在安全时机真正reboot
 *
 * @return true: 有待执行的重启, false: 无待执行的重启
 */
bool dtu_storage_is_reboot_pending(void);

/**
 * @brief 设置当前模式
 * @details 正常启动路径由dtu_storage_load()设置
 *          除非后续接入新的模式来源，否则不建议业务层调用
 *
 * @param[in] mode 要设置的模式
 */
void dtu_storage_set_current_mode(dtu_mode_t mode);

/**
 * @brief 设置reboot pending标志
 * @details REBOOT命令handler只置位，不直接在协议处理栈里重启
 *          避免回包丢失
 *
 * @param[in] pending 是否有待执行的重启
 */
void dtu_storage_set_reboot_pending(bool pending);

/**
 * @brief 参数合法性检查
 * @details 用于协议body入参校验
 *
 * @param[in] mode/role/uart_cfg/dev_type 要检查的参数
 * @return true: 参数合法, false: 参数非法
 */
bool dtu_storage_is_valid_mode(uint8_t mode);
bool dtu_storage_is_valid_role(uint8_t role);
bool dtu_storage_is_valid_uart_cfg(const dtu_uart_cfg_t *cfg);
bool dtu_storage_is_valid_dev_type(uint8_t dev_type);

/**
 * @brief 受控修改当前缓存配置
 * @details SET_*命令只改RAM，COMMIT后才写入NV
 *
 * @param[in] cfg 串口配置
 * @return ERRCODE_SUCC成功，其他失败
 */
errcode_t dtu_storage_set_uart_cfg(const dtu_uart_cfg_t *cfg);

/**
 * @brief 在runtime白名单里按MAC查找条目
 * @details 返回index，未找到返回负数
 *
 * @param[in] mac 要查找的MAC地址
 * @return 白名单条目索引，未找到返回-1
 */
int32_t dtu_storage_find_wl_item(const uint8_t *mac);

/**
 * @brief 初始化白名单node的子配置
 * @details ADD_WL_ITEM新增条目时调用，保证node uart/modbus有默认值
 *
 * @param[out] item 要初始化的白名单条目
 */
void dtu_storage_init_wl_item_cfg(dtu_wl_item_t *item);

/**
 * @brief 获取当前UART RX profile
 * @details CONFIG追求低延迟，RUN追求批量化
 *          transport初始化时会读取这些参数
 *
 * @return RX profile枚举值
 */
dtu_rx_profile_t dtu_storage_rx_profile(void);
uint16_t dtu_storage_rx_notify_length(void);
uint8_t dtu_storage_rx_int_threshold(void);

/**
 * @brief 串口配置转换
 * @details 协议baud_level -> 驱动baudrate
 *
 * @param[in] baud_level 波特率等级
 * @return 实际波特率值
 */
uint32_t dtu_storage_uart_baudrate(uint8_t baud_level);

/**
 * @brief 将DTU协议里的uart_cfg转成驱动uart_attr_t
 * @details 填充SDK UART属性结构
 *
 * @param[out] uart_attr SDK UART属性结构
 * @param[in] cfg DTU协议串口配置
 */
void dtu_storage_fill_uart_attr(uart_attr_t *uart_attr, const dtu_uart_cfg_t *cfg);

/**
 * @brief 填充默认runtime配置
 * @details 首次烧录、NV无效、factory reset都会回到这套默认值
 *
 * @param[out] cfg 要填充的runtime配置
 */
void dtu_storage_set_default(dtu_runtime_cfg_t *cfg);

/**
 * @brief 获取设备MAC
 * @details 当前由Kconfig固定值提供
 *          如果后续改策略，只在storage内部收口
 *
 * @param[out] mac MAC地址存储缓冲
 */
void dtu_storage_get_device_mac(uint8_t *mac);

/**
 * @brief 获取设备名
 * @details 当前优先Kconfig，未配置时使用默认名
 *
 * @param[out] name_buf 设备名存储缓冲
 * @param[in] name_buf_len 缓冲长度
 * @return 实际设备名长度
 */
uint8_t dtu_storage_get_device_name(uint8_t *name_buf, uint8_t name_buf_len);

/**
 * @brief 枚举值转字符串
 * @details 统一给日志使用
 *
 * @param[in] role/parity/mode/profile 枚举值
 * @return 对应的字符串名称
 */
const char *dtu_storage_role_name(uint8_t role);
const char *dtu_storage_parity_name(uint8_t parity);
const char *dtu_storage_mode_name(dtu_mode_t mode);
const char *dtu_storage_rx_profile_name(dtu_rx_profile_t profile);

/**
 * @brief 从NV加载配置
 * @details 并读取GPIO13决定当前模式
 *          NV无效时使用默认配置，但函数仍返回成功
 *
 * @return ERRCODE_SUCC成功，其他失败
 */
errcode_t dtu_storage_load(void);

/**
 * @brief 将当前runtime配置写入NV
 * @details 当前设计会拆分base配置和白名单shard，避免单key过大
 *
 * @return ERRCODE_SUCC成功，其他失败
 */
errcode_t dtu_storage_commit(void);

/**
 * @brief 恢复默认配置并写入NV
 * @details 清除所有自定义配置，恢复出厂默认值
 *
 * @return ERRCODE_SUCC成功，其他失败
 */
errcode_t dtu_storage_factory_reset(void);

#endif
