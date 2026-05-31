CROSS ?= /home/sueiny/rk3506_linux6.1_v1.2.0/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
CC := $(CROSS)gcc
STRIP := $(CROSS)strip

APP_DIR := .
SLE_SDK := ../../evb3506_sle_app_demo_v112
BUILD_DIR := $(APP_DIR)/build
TARGET := $(APP_DIR)/sle_data_app

INCLUDES := \
	-I$(APP_DIR)/inc \
	-I$(SLE_SDK)/include/bsle/common \
	-I$(SLE_SDK)/include/bsle/sle \
	-I$(SLE_SDK)/include/libc_sec/include \
	-I$(SLE_SDK)/include/drv/include \
	-I$(SLE_SDK)/include/osal/include

CFLAGS += $(INCLUDES) -Wall -Wextra -Os
LDFLAGS += -lm -lpthread -lrt -ldl -Wl,--gc-sections
SLE_LIB := $(SLE_SDK)/app_sample/sle/sle_uuid_client/lib/libsle_host.a

SRCS := $(wildcard $(APP_DIR)/src/*.c)
OBJS := $(patsubst $(APP_DIR)/src/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean tidy push

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo " LD $@"
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -Xlinker "-(" $(SLE_LIB) -Xlinker "-)"
	$(STRIP) --strip-unneeded -s $@

$(BUILD_DIR)/%.o: $(APP_DIR)/src/%.c
	@echo " CC $@"
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

tidy:
	rm -rf $(BUILD_DIR) $(APP_DIR)/src/*.o

clean:
	rm -rf $(TARGET) $(BUILD_DIR) $(APP_DIR)/src/*.o

push: all
	adb shell 'mkdir -p /userdata/gateway/bin /userdata/gateway/config /userdata/gateway/data/sle'
	adb push $(TARGET) /userdata/gateway/bin/
	adb push $(APP_DIR)/sle_data_app.json /userdata/gateway/config/
	adb shell 'chmod +x /userdata/gateway/bin/sle_data_app'
