cmake_minimum_required(VERSION 3.10)

project(gatewayd CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SKIP_RPATH TRUE)

find_package(Threads REQUIRED)

set(GATEWAYD_MOSQUITTO_ROOT "" CACHE PATH "Optional mosquitto sysroot/staging prefix")
if(GATEWAYD_MOSQUITTO_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${GATEWAYD_MOSQUITTO_ROOT}")
endif()

set(GATEWAYD_SQLITE_ROOT "" CACHE PATH "Optional sqlite sysroot/staging prefix")
if(GATEWAYD_SQLITE_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${GATEWAYD_SQLITE_ROOT}")
endif()

find_path(MOSQUITTO_INCLUDE_DIR
    NAMES mosquitto.h
    HINTS
        "${GATEWAYD_MOSQUITTO_ROOT}/usr/include"
        "${GATEWAYD_MOSQUITTO_ROOT}/include"
)

find_library(MOSQUITTO_LIBRARY
    NAMES mosquitto
    HINTS
        "${GATEWAYD_MOSQUITTO_ROOT}/usr/lib"
        "${GATEWAYD_MOSQUITTO_ROOT}/lib"
)

if(NOT MOSQUITTO_INCLUDE_DIR OR NOT MOSQUITTO_LIBRARY)
    message(FATAL_ERROR
        "libmosquitto not found. Build mosquitto into Buildroot staging first, "
        "or configure with -DGATEWAYD_MOSQUITTO_ROOT=/path/to/sysroot")
endif()

find_path(SQLITE3_INCLUDE_DIR
    NAMES sqlite3.h
    HINTS
        "${GATEWAYD_SQLITE_ROOT}/usr/include"
        "${GATEWAYD_SQLITE_ROOT}/include"
)

find_library(SQLITE3_LIBRARY
    NAMES sqlite3
    HINTS
        "${GATEWAYD_SQLITE_ROOT}/usr/lib"
        "${GATEWAYD_SQLITE_ROOT}/lib"
)

set(GATEWAYD_SQLITE_SOURCES src/storage/sqlite_db.cpp)

if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR
        "SQLite not found. Build sqlite into Buildroot staging first, "
        "or configure with -DGATEWAYD_SQLITE_ROOT=/path/to/sqlite-prefix")
endif()
message(STATUS "SQLite found: ${SQLITE3_LIBRARY}")

add_executable(gatewayd
    src/main.cpp
    src/app/collect_worker.cpp
    src/app/command_manager.cpp
    src/app/gateway_app.cpp
    src/app/network_worker.cpp
    src/app/publish_manager.cpp
    src/codec/thingskit_codec.cpp
    src/command/command_payload_codec.cpp
    src/common/file_utils.cpp
    src/common/device_model.cpp
    src/common/logger.cpp
    src/common/time_utils.cpp
    src/config/config_manager.cpp
    src/cloud/mqtt_cloud_client.cpp
    src/command/command_executor.cpp
    src/command/command_router.cpp
    src/command/command_validator.cpp
    src/command/thing_model_service_registry.cpp
    src/datasource/mock_data_source.cpp
    src/datasource/sle_data_source.cpp
    src/app/sle_ipc_worker.cpp
    src/datasource/ipc_receiver.cpp
    src/datasource/ipc_cmd_sender.cpp
    src/datasource/route_table.cpp
    src/codec/sle_frame_parser.cpp
    src/codec/modbus_parser.cpp
    src/state/device_state_store.cpp
    src/state/state_patch_codec.cpp
    src/storage/cache_store.cpp
    ${GATEWAYD_SQLITE_SOURCES}
    src/network/cellular_provider.cpp
    src/network/ethernet_provider.cpp
    src/network/i_network_provider.cpp
    src/network/net_manager.cpp
    src/network/network_utils.cpp
    src/network/netlink_utils.cpp
    src/network/wifi_provider.cpp
)

target_include_directories(gatewayd PRIVATE
    include
    third_party
    ${MOSQUITTO_INCLUDE_DIR}
    ${SQLITE3_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../sle_data_app/inc
)

target_compile_options(gatewayd PRIVATE
    -Wall
    -Wextra
    -Wno-psabi
)

target_link_libraries(gatewayd PRIVATE
    Threads::Threads
    ${MOSQUITTO_LIBRARY}
    ${SQLITE3_LIBRARY}
)

target_link_options(gatewayd PRIVATE -Wl,-rpath,/userdata/gateway/lib)

target_link_options(gatewayd PRIVATE
    -Wl,--allow-shlib-undefined
)

install(TARGETS gatewayd
    RUNTIME DESTINATION bin
)
