PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR := $(PROJECT_DIR)/build-cmake
TOOLCHAIN_FILE := $(PROJECT_DIR)/cmake/rk3506-armhf-toolchain.cmake
TARGET := edge_gateway_demo
ADB_DIR ?= /userdata/edge_gateway

.PHONY: all configure clean push shell

all: configure
	cmake --build $(BUILD_DIR) --parallel

configure:
	cmake -S $(PROJECT_DIR) \
		-B $(BUILD_DIR) \
		-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_FILE) \
		-DCMAKE_BUILD_TYPE=Release

push: all
	adb shell "mkdir -p $(ADB_DIR)"
	adb push $(BUILD_DIR)/$(TARGET) $(ADB_DIR)/

shell:
	adb shell

clean:
	rm -rf $(BUILD_DIR)
