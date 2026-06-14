#ifndef IPC_CMD_PROTOCOL_H
#define IPC_CMD_PROTOCOL_H

/*
 * IPC 命令协议定义
 * gatewayd 和 sle_data_app 之间通过 Unix Socket (cmd_socket) 传输命令。
 * 帧格式：2 字节 LE 长度前缀 + 帧体（与数据通道一致）。
 */

#include <stdint.h>

/* ── 帧类型 ── */
#define IPC_FRAME_TYPE_CMD_REQUEST   0x01   /* gatewayd → sle_data_app */
#define IPC_FRAME_TYPE_CMD_RESPONSE  0x02   /* sle_data_app → gatewayd */

/* ── 方法类型 ── */
#define CMD_METHOD_SET_RELAY         1      /* 继电器控制 */
#define CMD_METHOD_SET_MODE          2      /* 控制模式 */
#define CMD_METHOD_SET_COLLECT_CYCLE 3      /* 采集周期 */
#define CMD_METHOD_TRIGGER_COLLECT   4      /* 触发采集 */
#define CMD_METHOD_REBOOT            5      /* 重启 DTU */

/* ── 响应码 ── */
#define CMD_RESULT_OK                0      /* 执行成功 */
#define CMD_RESULT_FAILED            1      /* 执行失败 */
#define CMD_RESULT_TIMEOUT           2      /* 执行超时 */
#define CMD_RESULT_UNSUPPORTED       3      /* 不支持的方法 */

/* ── 帧大小限制 ── */
#define IPC_CMD_MAX_PARAM_LEN        256
#define IPC_CMD_MAX_DATA_LEN         256

/*
 * 命令请求帧（gatewayd → sle_data_app）
 *
 * 布局：
 *   [0]      frame_type (1B) = IPC_FRAME_TYPE_CMD_REQUEST
 *   [1-2]    seq (2B LE) = 序列号，用于匹配响应
 *   [3]      dtu_id (1B) = 目标 DTU ID (1-255)
 *   [4]      method (1B) = CMD_METHOD_*
 *   [5-6]    param_len (2B LE) = 参数数据长度
 *   [7-N]    param_data = 参数数据（JSON 或原始字节）
 */
typedef struct {
    uint8_t  frame_type;
    uint16_t seq;
    uint8_t  dtu_id;
    uint8_t  method;
    uint16_t param_len;
    uint8_t  param_data[IPC_CMD_MAX_PARAM_LEN];
} __attribute__((packed)) ipc_cmd_request_t;

/*
 * 命令响应帧（sle_data_app → gatewayd）
 *
 * 布局：
 *   [0]      frame_type (1B) = IPC_FRAME_TYPE_CMD_RESPONSE
 *   [1-2]    seq (2B LE) = 与请求匹配的序列号
 *   [3]      result_code (1B) = CMD_RESULT_*
 *   [4-5]    data_len (2B LE) = 响应数据长度
 *   [6-N]    data = 响应数据（JSON）
 */
typedef struct {
    uint8_t  frame_type;
    uint16_t seq;
    uint8_t  result_code;
    uint16_t data_len;
    uint8_t  data[IPC_CMD_MAX_DATA_LEN];
} __attribute__((packed)) ipc_cmd_response_t;

#endif /* IPC_CMD_PROTOCOL_H */
