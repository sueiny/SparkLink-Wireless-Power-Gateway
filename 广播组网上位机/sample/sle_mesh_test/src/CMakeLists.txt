#===============================================================================
# @brief    cmake file for SLE Mesh Test sources
#===============================================================================
set(SOURCES "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/SM_mesh.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SM_mesh_adv.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SM_mesh_scan.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SM_mesh_proto.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SM_mesh_node.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SM_mesh_dongle.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SM_mesh_uart.c"
    PARENT_SCOPE)
