# CMake Patterns for Embedded Linux Gateway

## Cross-Compilation Configuration

### Sysroot Management

```cmake
# Support multiple dependency roots for cross-compilation
set(GATEWAYD_MOSQUITTO_ROOT "" CACHE PATH "Optional mosquitto sysroot/staging prefix")
set(GATEWAYD_SQLITE_ROOT "" CACHE PATH "Optional sqlite sysroot/staging prefix")

# Append to CMAKE_PREFIX_PATH for find_package/find_library
if(GATEWAYD_MOSQUITTO_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${GATEWAYD_MOSQUITTO_ROOT}")
endif()
```

### Finding Dependencies

```cmake
# Use find_path for headers with multiple search paths
find_path(MOSQUITTO_INCLUDE_DIR
    NAMES mosquitto.h
    HINTS
        "${GATEWAYD_MOSQUITTO_ROOT}/usr/include"
        "${GATEWAYD_MOSQUITTO_ROOT}/include"
)

# Use find_library for shared/static libraries
find_library(MOSQUITTO_LIBRARY
    NAMES mosquitto
    HINTS
        "${GATEWAYD_MOSQUITTO_ROOT}/usr/lib"
        "${GATEWAYD_MOSQUITTO_ROOT}/lib"
)

# Validate dependencies found
if(NOT MOSQUITTO_INCLUDE_DIR OR NOT MOSQUITTO_LIBRARY)
    message(FATAL_ERROR
        "libmosquitto not found. Build mosquitto into Buildroot staging first, "
        "or configure with -DGATEWAYD_MOSQUITTO_ROOT=/path/to/sysroot")
endif()
```

## Target Configuration

### C++ Standard

```cmake
# Enforce C++17 without extensions
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

### Compiler Options

```cmake
target_compile_options(gatewayd PRIVATE
    -Wall                    # Enable all warnings
    -Wextra                  # Enable extra warnings
    -Wno-psabi               # Suppress ABI warnings (ARM cross-compilation)
)
```

### Linker Options

```cmake
# Set runtime library path for embedded system
target_link_options(gatewayd PRIVATE -Wl,-rpath,/userdata/gateway/lib)

# Allow undefined symbols in shared libraries (common in cross-compilation)
target_link_options(gatewayd PRIVATE -Wl,--allow-shlib-undefined)
```

## Source File Organization

### Explicit Source Listing

```cmake
# List all source files explicitly (no globbing)
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
    src/state/device_state_store.cpp
    src/state/state_patch_codec.cpp
    src/storage/cache_store.cpp
    ${GATEWAYD_SQLITE_SOURCES}
    src/network/cellular_provider.cpp
    src/network/ethernet_provider.cpp
    src/network/i_network_provider.cpp
    src/network/net_manager.cpp
    src/network/network_utils.cpp
    src/network/wifi_provider.cpp
)
```

### Include Directories

```cmake
target_include_directories(gatewayd PRIVATE
    include                    # Project headers
    third_party                # Third-party headers (json.hpp, etc.)
    ${MOSQUITTO_INCLUDE_DIR}   # Mosquitto headers
    ${SQLITE3_INCLUDE_DIR}     # SQLite headers
)
```

## Installation

```cmake
# Install binary to standard location
install(TARGETS gatewayd
    RUNTIME DESTINATION bin
)
```

## Build Commands

### Native Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Cross-Compilation Build

```bash
mkdir build && cd build
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake \
    -DGATEWAYD_MOSQUITTO_ROOT=/path/to/sysroot \
    -DGATEWAYD_SQLITE_ROOT=/path/to/sysroot
make -j$(nproc)
```

### CMake Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `CMAKE_TOOLCHAIN_FILE` | Cross-compilation toolchain file | `/opt/toolchain.cmake` |
| `GATEWAYD_MOSQUITTO_ROOT` | Mosquitto sysroot prefix | `/opt/staging` |
| `GATEWAYD_SQLITE_ROOT` | SQLite sysroot prefix | `/opt/staging` |
| `CMAKE_INSTALL_PREFIX` | Installation prefix | `/usr/local` |
