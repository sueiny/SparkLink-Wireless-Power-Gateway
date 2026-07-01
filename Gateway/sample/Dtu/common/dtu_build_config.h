/**
 * @file dtu_build_config.h
 * @brief DTU 编译期默认值、板级映射和协议 ABI 常量。
 */

#ifndef DTU_BUILD_CONFIG_H
#define DTU_BUILD_CONFIG_H

#include "gpio.h"
#include "pinctrl.h"
#include "uart.h"

/* 常改配置：默认 UART、任务、缓存和帧大小。 */

/* UART0 固定配置，避免 NV 异常时配置/调试口不可达。 */
#define DTU_CFG_UART0_DEFAULT_BAUD_LEVEL      0x07
#define DTU_CFG_UART0_DEFAULT_PARITY          0x00
#define DTU_CFG_UART0_DEFAULT_STOP_BITS       0x01
#define DTU_CFG_UART0_DEFAULT_DATA_BITS       0x08
#define DTU_CFG_UART0_DEFAULT_CFG_INIT        { \
    .baud_level = DTU_CFG_UART0_DEFAULT_BAUD_LEVEL, \
    .parity = DTU_CFG_UART0_DEFAULT_PARITY, \
    .stop_bits = DTU_CFG_UART0_DEFAULT_STOP_BITS, \
    .data_bits = DTU_CFG_UART0_DEFAULT_DATA_BITS \
}

/* UART1/485 默认值用于首次启动、NV 无效和恢复出厂。 */
#define DTU_CFG_485_DEFAULT_BAUD_LEVEL        0x03
#define DTU_CFG_485_DEFAULT_PARITY            0x01
#define DTU_CFG_485_DEFAULT_STOP_BITS         0x01
#define DTU_CFG_485_DEFAULT_DATA_BITS         0x08
#define DTU_CFG_485_DEFAULT_CFG_INIT          { \
    .baud_level = DTU_CFG_485_DEFAULT_BAUD_LEVEL, \
    .parity = DTU_CFG_485_DEFAULT_PARITY, \
    .stop_bits = DTU_CFG_485_DEFAULT_STOP_BITS, \
    .data_bits = DTU_CFG_485_DEFAULT_DATA_BITS \
}

/* init task 只启动 manager，启动完成后退出。 */
#define DTU_CFG_INIT_TASK_NAME                "DtuInitTask"
#define DTU_CFG_INIT_TASK_STACK_SIZE          0x2000
#define DTU_CFG_INIT_TASK_PRIO                24

/* UART0、UART1/485、BLE、SLE 共用同一组任务栈和优先级。 */
#define DTU_CFG_UART0_TASK_NAME               "DtuUartTask"
#define DTU_CFG_485_TASK_NAME                 "Dtu485Task"
#define DTU_CFG_BLE_TASK_NAME                 "DtuBleTask"
#define DTU_CFG_SLE_TASK_NAME                 "DtuSleTask"
#define DTU_CFG_TRANSPORT_TASK_STACK_SIZE     0x1200
#define DTU_CFG_TRANSPORT_TASK_PRIO           25

/* batch 越小 CONFIG 响应越快，越大 RUN 透传吞吐越好。 */
#define DTU_CFG_TRANSPORT_RX_BATCH_SIZE       64

/* ring buffer 用于跨驱动回调和 transport task 搬运数据。 */
#define DTU_CFG_RX_DRIVER_BUFFER_SIZE         512
#define DTU_CFG_RING_BUFFER_SIZE              2048

/* CONFIG 按字节唤醒保证解析延迟，RUN 批量唤醒减少透传开销。 */
#define DTU_CFG_MODE_CONFIG_RX_NOTIFY_LENGTH  1
#define DTU_CFG_MODE_RUN_RX_NOTIFY_LENGTH     32
#define DTU_CFG_MODE_CONFIG_RX_INT_THRESHOLD  UART_FIFO_INT_RX_LEVEL_1_CHARACTER
#define DTU_CFG_MODE_RUN_RX_INT_THRESHOLD     UART_FIFO_INT_RX_LEVEL_1_2

/* CONFIG 单帧 body 上限；修改会影响协议和上位机工具兼容性。 */
#define DTU_CFG_MAX_FRAME_BODY                192

/* RUN mesh 预留参数；当前透明桥接不改 payload。 */
#define DTU_CFG_RUN_PACKET_MAX_PAYLOAD        192
#define DTU_CFG_RUN_PACKET_HEADER_SIZE        10

/* 白名单容量需与 NV 分片数量和每片条目数保持一致。 */
#define DTU_CFG_MAX_NAME_LEN                  31
#define DTU_CFG_MAX_MODBUS_ITEMS              8
#define DTU_CFG_MAX_WL_ITEMS                  128
#define DTU_CFG_NV_WL_SHARD_COUNT             8
#define DTU_CFG_NV_WL_ITEMS_PER_SHARD         16
#define DTU_CFG_WL_FRAGMENT_BODY_MAX          89

/* REBOOT 回包后延迟复位，避免响应帧还没发完。 */
#define DTU_CFG_REBOOT_DELAY_MS               20

/* task 空转等待间隔：越小越灵敏，越大空转开销越低。 */
#define DTU_CFG_TASK_IDLE_RETRY_MS            1

/* 板级映射：改引脚前先核对原理图。 */

/* UART0：CONFIG 下做配置口，RUN 下做 PC 观察口。 */
#define DTU_CFG_UART_BUS                      UART_BUS_0
#define DTU_CFG_UART_TX_PIN                   17
#define DTU_CFG_UART_RX_PIN                   18
#define DTU_CFG_UART_PIN_MODE                 PIN_MODE_1

/* UART1：RUN 模式 485 总线口。 */
#define DTU_CFG_485_UART_BUS                  UART_BUS_1
#define DTU_CFG_485_UART_TX_PIN               16
#define DTU_CFG_485_UART_RX_PIN               15//第一版需要反过来
#define DTU_CFG_485_UART_PIN_MODE             PIN_MODE_1

/* 模式拨码：高电平 RUN，低电平 CONFIG；内部上拉默认 RUN。 */
#define DTU_CFG_MODE_SWITCH_PIN               13
#define DTU_CFG_MODE_SWITCH_PIN_MODE          PIN_MODE_0
#define DTU_CFG_MODE_SWITCH_PIN_PULL          PIN_PULL_TYPE_UP

/* 恢复出厂按键：IO14 默认上拉，按下保持低电平 1s 后写入默认配置。 */
#define DTU_CFG_FACTORY_KEY_PIN               14
#define DTU_CFG_FACTORY_KEY_PIN_MODE          PIN_MODE_0
#define DTU_CFG_FACTORY_KEY_PULL              PIN_PULL_TYPE_UP
#define DTU_CFG_FACTORY_KEY_HOLD_MS           3000

/* 状态灯：CONFIG 红，RUN ROOT 绿，RUN NODE 蓝；活动灯白色闪烁。 */
#define DTU_CFG_STATE_LED_BLUE_PIN             2
#define DTU_CFG_STATE_LED_GREEN_PIN            1  
#define DTU_CFG_STATE_LED_RED_PIN              0            //LED1  
#define DTU_CFG_ACTIVITY_LED_BLUE_PIN         8
#define DTU_CFG_ACTIVITY_LED_GREEN_PIN        10
#define DTU_CFG_ACTIVITY_LED_RED_PIN          9            //LED2
#define DTU_CFG_LED_PIN_MODE                  PIN_MODE_0
#define DTU_CFG_ACTIVITY_LED_HOLD_MS          120 

/* 485 方向由 UART1_TX 控制：空闲高电平为接收，发送 0 bit 的低电平脉冲触发发送。 */
/* Kconfig 派生配置。 */

/* trace 默认关闭，高频 RX 日志会拖慢 transport task。 */
#if defined(CONFIG_DTU_TRACE_LOG)
#define DTU_CFG_LOG_TRACE_ENABLE              1
#else
#define DTU_CFG_LOG_TRACE_ENABLE              0
#endif

/* 设备名用于 BLE/SLE 广播和日志。 */
#if defined(CONFIG_DTU_DEVICE_NAME)
#define DTU_CFG_DEVICE_NAME                   CONFIG_DTU_DEVICE_NAME
#else
#define DTU_CFG_DEVICE_NAME                   "DTU_N01"
#endif

/* 固定 MAC 支持 AABBCCDDEEFF 或 AA:BB:CC:DD:EE:FF。 */
#if defined(CONFIG_DTU_FORCE_MENUCONFIG_MAC)
#define DTU_CFG_FORCE_MENUCONFIG_MAC          1
#else
#define DTU_CFG_FORCE_MENUCONFIG_MAC          0
#endif

#if defined(CONFIG_DTU_FIXED_MAC)
#define DTU_CFG_FIXED_MAC                     CONFIG_DTU_FIXED_MAC
#else
#define DTU_CFG_FIXED_MAC                     "A1:A2:A3:A4:A5:A6"
#endif

/* 协议 ABI：修改后必须同步 PC/Web/test tools。 */

/* 持久化 NV 布局变化时必须升级 DTU_CFG_NV_VERSION。 */
#define DTU_CFG_NV_MAGIC                      0x44545532U
#define DTU_CFG_NV_VERSION                    0x0005

/* 设备角色会持久化到 runtime 配置。 */
#define DTU_CFG_ROLE_NODE                     0x00
#define DTU_CFG_ROLE_ROOT                     0x01

/* 配置协议响应状态码。 */
#define DTU_CFG_STATUS_SUCC                   0x00
#define DTU_CFG_STATUS_CRC_ERR                0x01
#define DTU_CFG_STATUS_LEN_ERR                0x02
#define DTU_CFG_STATUS_CMD_ERR                0x03
#define DTU_CFG_STATUS_PARAM_ERR              0x04
#define DTU_CFG_STATUS_NOT_CONFIG             0x05
#define DTU_CFG_STATUS_ROLE_MISMATCH          0x06
#define DTU_CFG_STATUS_WL_FULL                0x07
#define DTU_CFG_STATUS_NOT_FOUND              0x08
#define DTU_CFG_STATUS_SAVE_FAIL              0x09
#define DTU_CFG_STATUS_BUSY                   0x0A

/* 配置协议命令字；handler 表在 config/dtu_config_commands.c。 */
#define DTU_CFG_CMD_READ_DEV_INFO             0x01
#define DTU_CFG_CMD_READ_UART_CFG             0x02
#define DTU_CFG_CMD_READ_MODBUS_CFG           0x03
#define DTU_CFG_CMD_READ_ROOT_WL_ALL          0x04
#define DTU_CFG_CMD_READ_ROOT_POWER           0x05
#define DTU_CFG_CMD_GET_MODE_STATUS           0x06
#define DTU_CFG_CMD_READ_WL_NODE_CFG          0x07
#define DTU_CFG_CMD_SET_ROLE                  0x10
#define DTU_CFG_CMD_SET_UART_CFG              0x11
#define DTU_CFG_CMD_SET_MODBUS_CFG            0x12
#define DTU_CFG_CMD_SET_ROOT_POWER            0x13
#define DTU_CFG_CMD_ADD_WL_ITEM               0x14
#define DTU_CFG_CMD_DEL_WL_ITEM               0x15
#define DTU_CFG_CMD_CLEAR_WL                  0x16
#define DTU_CFG_CMD_SET_WL_NODE_CFG           0x17
#define DTU_CFG_CMD_COMMIT                    0x20
#define DTU_CFG_CMD_REBOOT                    0x21
#define DTU_CFG_CMD_FACTORY_RESET             0x22

/* CONFIG 帧格式：AA 55 + cmd + seq + len_le + body + crc_le。 */
#define DTU_CFG_SOF0                          0xAA
#define DTU_CFG_SOF1                          0x55

#endif
