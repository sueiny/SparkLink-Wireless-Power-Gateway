#===============================================================================
# @brief    cmake file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
#===============================================================================
set(SOURCES "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test_cfg.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test_route.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test_proto.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test_uart.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test_link.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test_root.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test_relay.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ST_test_leaf.c"
    PARENT_SCOPE)
