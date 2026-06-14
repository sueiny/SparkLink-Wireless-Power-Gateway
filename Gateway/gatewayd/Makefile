PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR := $(PROJECT_DIR)/build-cmake
TOOLCHAIN_FILE := $(PROJECT_DIR)/cmake/rk3506-armhf-toolchain.cmake
TARGET := gatewayd
ADB_ROOT ?= /userdata/gateway
MOSQUITTO_ROOT ?=
SQLITE_ROOT ?=

.PHONY: all configure clean push shell

SQLITE_LIB_DIR := $(if $(SQLITE_ROOT),$(firstword $(wildcard $(SQLITE_ROOT)/usr/lib $(SQLITE_ROOT)/lib)),)

all: configure
	cmake --build $(BUILD_DIR) --parallel

configure:
	cmake -S $(PROJECT_DIR) \
		-B $(BUILD_DIR) \
		-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
		-DCMAKE_BUILD_TYPE=Release \
		-DGATEWAYD_MOSQUITTO_ROOT=$(MOSQUITTO_ROOcd "$(dirname "$0")"T) \
		-DGATEWAYD_SQLITE_ROOT=$(SQLITE_ROOT)

push: all
	adb shell "mkdir -p $(ADB_ROOT)/bin $(ADB_ROOT)/config $(ADB_ROOT)/lib $(ADB_ROOT)/data/log $(ADB_ROOT)/data/cache $(ADB_ROOT)/data/state"
	adb push $(BUILD_DIR)/$(TARGET) $(ADB_ROOT)/bin/
	@if [ -n "$(SQLITE_LIB_DIR)" ] && ls "$(SQLITE_LIB_DIR)"/libsqlite3.so* >/dev/null 2>&1; then \
		echo "Pushing sqlite runtime libs from $(SQLITE_LIB_DIR)"; \
		adb push "$(SQLITE_LIB_DIR)"/libsqlite3.so* $(ADB_ROOT)/lib/; \
	else \
		echo "No sqlite runtime libs found under SQLITE_ROOT; skip sqlite lib push"; \
	fi
	@if [ -n "$(SQLITE_ROOT)" ] && [ -x "$(SQLITE_ROOT)/bin/sqlite3" ]; then \
		echo "Pushing sqlite3 cli from $(SQLITE_ROOT)/bin"; \
		adb push "$(SQLITE_ROOT)/bin/sqlite3" $(ADB_ROOT)/bin/; \
	fi
	adb push $(PROJECT_DIR)/config/gateway_config.json $(ADB_ROOT)/config/
	adb shell "rm -rf $(ADB_ROOT)/things_model"
	adb push $(PROJECT_DIR)/things_model $(ADB_ROOT)/things_model
	adb push $(PROJECT_DIR)/scripts/net $(ADB_ROOT)/scripts/
	adb shell "chmod +x $(ADB_ROOT)/scripts/net/*.sh"

shell:
	adb shell

clean:
	rm -rf $(BUILD_DIR)
