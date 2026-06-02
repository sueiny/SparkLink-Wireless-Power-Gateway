#ifndef GATEWAY_SLE_MODBUS_SIM_H
#define GATEWAY_SLE_MODBUS_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "server_connections.h"

/*
 * Modbus 仿真帧生成器。
 *
 * 职责：根据 modbus_type 生成仿真 Modbus RTU 响应帧（含 CRC16），
 * 与 MockDataSource 的模拟公式对齐。
 *
 * 生成的帧通过 notify_printer_enqueue_packet 入队，
 * 走正常日志 + IPC 通道到达 gatewayd。
 */

/* 设备类型枚举，与 gatewayd 的 modbus_type 一致 */
#define MODBUS_TYPE_METER    2   /* 单相电表 */
#define MODBUS_TYPE_ENV      3   /* 温湿度传感器 */
#define MODBUS_TYPE_RELAY    4   /* 继电器 */

/* 寄存器数量 */
#define MODBUS_METER_REG_COUNT   8   /* 电表：V/I/P/PF/F/E_hi/E_lo/relay */
#define MODBUS_ENV_REG_COUNT     2   /* 温湿度：T/H */
#define MODBUS_RELAY_REG_COUNT   2   /* 继电器：state/mode */

/* Modbus 帧最大长度: addr(1) + func(1) + bytes(1) + data(16) + crc(2) = 21 */
#define MODBUS_FRAME_MAX_LEN     24

/*
 * 初始化仿真状态。在 sle_manager_init 之后调用。
 * server_count: 已知的 DTU 数量（对应 config 中的设备数）。
 */
void modbus_sim_init(int server_count);

/*
 * 为指定 server_index 生成一帧仿真 Modbus RTU 响应。
 * tick: 全局 tick 计数（每秒 +1）。
 * server_index: 连接表索引（0-7）。
 * modbus_type: 设备类型（2=电表, 3=温湿度, 4=继电器）。
 * out_buf: 输出缓冲区，至少 MODBUS_FRAME_MAX_LEN 字节。
 * out_len: 输出帧实际长度。
 * 返回 true 表示成功。
 */
bool modbus_sim_generate(uint64_t tick, int server_index, int modbus_type,
    uint8_t *out_buf, uint16_t *out_len);

#endif
