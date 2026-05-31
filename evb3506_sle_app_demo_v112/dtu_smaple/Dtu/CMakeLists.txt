#===============================================================================
# @file     CMakeLists.txt
# @brief    DTU项目CMake构建配置文件
# @details  本文件定义了DTU项目的源文件列表和头文件路径，
#           用于CMake构建系统编译DTU应用程序。
#
# @version  2.0
# @date     2026-05-20
#
# @par 构建说明：
#       - 本文件由CMake构建系统自动包含
#       - 修改后需要重新运行CMake配置
#       - 源文件列表按模块组织，便于维护
#
# @par 模块组织：
#       - common/：公共模块（日志、类型定义）
#       - manager/：服务管理器模块
#       - config/：配置协议处理模块
#       - run/：运行态业务模块
#       - storage/：存储中心模块
#       - board/：板级支持模块
#       - transport/：传输通道模块
#
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
#===============================================================================

#===============================================================================
# 源文件列表
# @brief DTU项目的所有C源文件
# @details 按模块组织，每个模块包含相关的源文件
#===============================================================================
set(SOURCES_LIST
    # 公共模块：日志、类型定义等
    ${CMAKE_CURRENT_SOURCE_DIR}/common/dtu_log.c
    # 服务管理器：初始化顺序、输入分流、输出统一
    ${CMAKE_CURRENT_SOURCE_DIR}/manager/dtu_service.c
    # 配置协议处理：AA55帧解析、命令分发
    ${CMAKE_CURRENT_SOURCE_DIR}/config/dtu_config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/config/dtu_config_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/config/dtu_config_commands.c
    # 运行态业务：数据透传、组网
    ${CMAKE_CURRENT_SOURCE_DIR}/run/dtu_run.c
    # 存储中心：配置持久化、NV存储
    ${CMAKE_CURRENT_SOURCE_DIR}/storage/dtu_storage.c
    # 板级支持：DIP检测、LED控制
    ${CMAKE_CURRENT_SOURCE_DIR}/board/dtu_board.c
    # 传输通道：UART、BLE、SLE
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_ble.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_sle.c
    # 主入口文件
    ${CMAKE_CURRENT_SOURCE_DIR}/dtu_main.c
)

#===============================================================================
# 头文件路径列表
# @brief DTU项目的所有头文件搜索路径
# @details 包含所有模块的头文件目录，便于编译器查找头文件
#===============================================================================
set(HEADER_LIST
    # 项目根目录
    ${CMAKE_CURRENT_SOURCE_DIR}
    # 公共模块头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    # 服务管理器头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/manager
    # 配置协议处理头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    # 运行态业务头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/run
    # 存储中心头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/storage
    # 板级支持头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/board
    # 传输通道头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/transport
)

#===============================================================================
# 导出变量到父作用域
# @details 将源文件列表和头文件路径导出到父CMakeLists.txt
#          供上层构建系统使用
#===============================================================================
set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)
