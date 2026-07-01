# DTU 源文件列表，按运行层次分组。
set(SOURCES_LIST
    # 公共模块
    ${CMAKE_CURRENT_SOURCE_DIR}/common/dtu_log.c
    # 管理器
    ${CMAKE_CURRENT_SOURCE_DIR}/manager/dtu_service.c
    # CONFIG 协议
    ${CMAKE_CURRENT_SOURCE_DIR}/config/dtu_config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/config/dtu_config_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/config/dtu_config_commands.c
    # RUN 透传
    ${CMAKE_CURRENT_SOURCE_DIR}/run/dtu_run.c
    # 存储与板级
    ${CMAKE_CURRENT_SOURCE_DIR}/storage/dtu_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/board/dtu_board.c
    # 传输通道
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_ble.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/dtu_channel_sle.c
    # 入口
    ${CMAKE_CURRENT_SOURCE_DIR}/dtu_main.c
)

# DTU 模块头文件搜索路径。
set(HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/manager
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/run
    ${CMAKE_CURRENT_SOURCE_DIR}/storage
    ${CMAKE_CURRENT_SOURCE_DIR}/board
    ${CMAKE_CURRENT_SOURCE_DIR}/transport
)

set(SOURCES "${SOURCES}" ${SOURCES_LIST} PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" ${HEADER_LIST} PARENT_SCOPE)
