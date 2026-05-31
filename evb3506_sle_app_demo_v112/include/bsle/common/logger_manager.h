/**
 * Copyright (c) @CompanyNameMagicTag 2024. All rights reserved.
 *
 * Description: logger manager module.
 */

/**
 * @defgroup common logger manager
 * @ingroup  SLE
 * @{
 */
#ifndef H_LOGGER_MANAGER_H
#define H_LOGGER_MANAGER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @if Eng
 * @brief Enum of log level.
 * @else
 * @brief 日志级别枚举.
 * @endif
 */
typedef enum {
    H_LOG_LEVEL_NONE,
    H_LOG_LEVEL_ERROR,
    H_LOG_LEVEL_WARNING,
    H_LOG_LEVEL_INFO,
    H_LOG_LEVEL_DBG,
    H_LOG_LEVEL_MAX,
} h_log_level_e;


/**
 * @if Eng
 * @brief Callback invoked when log print.
 * @par This interface is used to register the log printing function so that logs can be printed in the customer system.
 * @attention This interface is applicable to Linux.
 * @param [in] log_level log level.
 * @param [in] fmt log print formatter parameters.
 * @par Dependency:
 * @else
 * @brief  日志打印接口。
 * @par    该接口用于用户注册日志打印函数，以便日志可以在客户系统打印。
 * @attention  1. 适用于Linux。
 * @param [in] log_level 日志打印级别。
 * @param [in] fmt 日志打印格式化参数。
 * @retval 无返回值。
 * @par 依赖:
 * @endif
 */
typedef void (*logger_func) (uint8_t log_level, const char* fmt, ...);

/**
 * @if Eng
 * @brief  Registering the logger print function.
 * @par    Registering the logger print function.
 * @attention Applicable to the Linux system.
 * @param [in] log  logger print function.
 * @retval error code.
 * @else
 * @brief  注册日志打印函数。
 * @par    注册日志打印函数。
 * @attention 适用于linux系统。
 * @param [in] log  日志打印函数。
 * @retval 执行结果错误码。
 * @endif
 */
void h_logger_register(logger_func log);

#ifdef __cplusplus
}
#endif
#endif /* H_LOGGER_MANAGER_H */
/**
 * @}
 */
