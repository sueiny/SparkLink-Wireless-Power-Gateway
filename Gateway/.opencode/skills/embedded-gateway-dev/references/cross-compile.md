# Cross-Compilation Guide for RK3506

## Toolchain Setup

### ARM64 Toolchain

```bash
# Install aarch64-linux-gnu toolchain (Ubuntu/Debian)
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

# Verify installation
aarch64-linux-gnu-gcc --version
aarch64-linux-gnu-g++ --version
```

### CMake Toolchain File

Create `toolchain-aarch64.cmake`:

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Specify the cross compiler
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Where to look for target system libraries
set(CMAKE_FIND_ROOT_PATH /opt/staging)

# Search for programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

## Build Configuration

### Buildroot Sysroot

If using Buildroot, the sysroot is typically at:
```
/output/staging/
```

Contains:
- `/usr/include/` - Headers
- `/usr/lib/` - Libraries
- `/lib/` - System libraries

### CMake Build Command

```bash
# Create build directory
mkdir build-rk3506 && cd build-rk3506

# Configure with cross-compilation
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake \
    -DGATEWAYD_MOSQUITTO_ROOT=/path/to/buildroot/output/staging \
    -DGATEWAYD_SQLITE_ROOT=/path/to/buildroot/output/staging \
    -DCMAKE_INSTALL_PREFIX=/usr

# Build
make -j$(nproc)

# Install to staging (optional)
make DESTDIR=/path/to/buildroot/output/staging install
```

## Dependency Management

### Mosquitto

```bash
# Build mosquitto in Buildroot
make mosquitto

# Verify in staging
ls /output/staging/usr/lib/libmosquitto*
ls /output/staging/usr/include/mosquitto.h
```

### SQLite

```bash
# Build sqlite in Buildroot
make sqlite

# Verify in staging
ls /output/staging/usr/lib/libsqlite3*
ls /output/staging/usr/include/sqlite3.h
```

### JSON Library (nlohmann/json)

```bash
# Header-only library - just copy to third_party/
cp json.hpp /path/to/gatewayd/third_party/
```

## Build Scripts

### Simple Build Script

Create `build-rk3506.sh`:

```bash
#!/bin/bash
set -e

BUILDROOT_DIR="/path/to/buildroot"
STAGING_DIR="${BUILDROOT_DIR}/output/staging"
BUILD_DIR="build-rk3506"

# Clean previous build
rm -rf ${BUILD_DIR}
mkdir ${BUILD_DIR}
cd ${BUILD_DIR}

# Configure
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake \
    -DGATEWAYD_MOSQUITTO_ROOT=${STAGING_DIR} \
    -DGATEWAYD_SQLITE_ROOT=${STAGING_DIR}

# Build
make -j$(nproc)

echo "Build complete: ${BUILD_DIR}/gatewayd"
```

### Deploy Script

Create `deploy-rk3506.sh`:

```bash
#!/bin/bash
set -e

TARGET_IP="192.168.1.100"
TARGET_USER="root"
BUILD_DIR="build-rk3506"

# Copy binary
scp ${BUILD_DIR}/gatewayd ${TARGET_USER}@${TARGET_IP}:/usr/bin/

# Copy config (if needed)
scp config/gateway_config.json ${TARGET_USER}@${TARGET_IP}:/etc/gateway/

# Restart service (if using systemd)
ssh ${TARGET_USER}@${TARGET_IP} "systemctl restart gatewayd"

echo "Deploy complete"
```

## Debugging

### GDB Remote Debugging

```bash
# On target: start with gdbserver
gdbserver :1234 /usr/bin/gatewayd --config /etc/gateway/gateway_config.json

# On host: connect with gdb
aarch64-linux-gnu-gdb build-rk3506/gatewayd
(gdb) target remote 192.168.1.100:1234
(gdb) break main
(gdb) continue
```

### Core Dump Analysis

```bash
# Enable core dumps on target
ulimit -c unlimited
echo "/tmp/core.%e.%p" > /proc/sys/kernel/core_pattern

# Run and generate core
./gatewayd --config /etc/gateway/gateway_config.json

# Copy core to host
scp /tmp/core.gatewayd.* user@host:/path/to/cores/

# Analyze on host
aarch64-linux-gnu-gdb build-rk3506/gatewayd /path/to/cores/core.gatewayd.1234
(gdb) bt  # Backtrace
(gdb) info registers
```

## Common Issues

### Missing Libraries

```
error: libmosquitto.so.1: cannot open shared object file: No such file or directory
```

**Solution**: 
1. Verify library exists in staging: `ls ${STAGING_DIR}/usr/lib/libmosquitto*`
2. Check rpath: `readelf -d gatewayd | grep rpath`
3. Set rpath in CMake: `target_link_options(gatewayd PRIVATE -Wl,-rpath,/usr/lib)`

### ABI Warnings

```
warning: mangling of 'va_list' changed in GCC 7.1
```

**Solution**: Suppress with `-Wno-psabi` (already in CMakeLists.txt)

### Linker Errors

```
undefined reference to `mosquitto_connect'
```

**Solution**:
1. Check library is linked: `target_link_libraries(gatewayd PRIVATE ${MOSQUITTO_LIBRARY})`
2. Check link order: libraries should come after objects
3. Verify symbol exists: `nm -D ${STAGING_DIR}/usr/lib/libmosquitto.so | grep mosquitto_connect`

## Optimization

### Release Build

```cmake
# Add optimization flags
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
```

### Size Optimization

```cmake
# For embedded systems with limited storage
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")
target_link_options(gatewayd PRIVATE -s)  # Strip symbols
```

### Link-Time Optimization (LTO)

```cmake
# Enable LTO for smaller binary
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
```

## Testing on Target

### Basic Test

```bash
# SSH to target
ssh root@192.168.1.100

# Test run
gatewayd --config /etc/gateway/gateway_config.json --mqtt-test

# Check logs
tail -f /var/log/gateway/gateway.log
```

### Systemd Service

Create `/etc/systemd/system/gatewayd.service`:

```ini
[Unit]
Description=Gateway Daemon
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/gatewayd --config /etc/gateway/gateway_config.json
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
# Enable and start
systemctl enable gatewayd
systemctl start gatewayd

# Check status
systemctl status gatewayd
journalctl -u gatewayd -f
```
