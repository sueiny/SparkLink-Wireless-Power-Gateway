/*
 * sle_cmd_handler.c — SLE 命令处理器
 *
 * 职责：
 *   接收来自 gatewayd 的命令请求，查找目标 DTU 的 SLE 连接，
 *   构建 Modbus 写请求并通过 ssapc_write_req() 发送到设备。
 *
 * 当前阶段：
 *   Mock 模式 — 不真正调用 SLE SDK，返回模拟成功。
 *   后续接入真实 SLE 时只需修改此文件内部实现。
 *
 * 线程安全：
 *   由 ipc_cmd_receiver 线程调用，不涉及共享状态。
 */

#include "sle_cmd_handler.h"

#include <stdio.h>
#include <string.h>

/* ── Mock 模式实现 ── */

/*
 * Mock: 处理 set_relay 命令。
 * 真实实现应构建 Modbus 写单个寄存器请求并调用 ssapc_write_req。
 */
static uint8_t handle_set_relay(const ipc_cmd_request_t *req,
                                uint8_t *resp_data, uint16_t *resp_data_len)
{
    /*
     * Modbus 写单个寄存器（功能码 0x06）：
     *   [0]   station_addr (DTU 下挂设备站号)
     *   [1]   func_code = 0x06
     *   [2-3] register_addr (继电器寄存器地址)
     *   [4-5] register_value (0=断开, 1=闭合)
     *   [6-7] CRC16
     *
     * 当前 Mock：直接返回成功。
     */
    fprintf(stderr, "[CMD][MOCK] set_relay dtu_id=%u param_len=%u\n",
            req->dtu_id, req->param_len);

    /* 构建 JSON 响应 */
    const char *resp = "{\"result\":1,\"message\":\"relay set (mock)\"}";
    uint16_t resp_len = (uint16_t)strlen(resp);
    if (resp_len > *resp_data_len)
        resp_len = *resp_data_len;
    memcpy(resp_data, resp, resp_len);
    *resp_data_len = resp_len;

    return CMD_RESULT_OK;
}

/*
 * Mock: 处理 set_mode 命令。
 */
static uint8_t handle_set_mode(const ipc_cmd_request_t *req,
                               uint8_t *resp_data, uint16_t *resp_data_len)
{
    fprintf(stderr, "[CMD][MOCK] set_mode dtu_id=%u param_len=%u\n",
            req->dtu_id, req->param_len);

    const char *resp = "{\"result\":1,\"message\":\"mode set (mock)\"}";
    uint16_t resp_len = (uint16_t)strlen(resp);
    if (resp_len > *resp_data_len)
        resp_len = *resp_data_len;
    memcpy(resp_data, resp, resp_len);
    *resp_data_len = resp_len;

    return CMD_RESULT_OK;
}

/*
 * Mock: 处理 set_collect_cycle 命令。
 */
static uint8_t handle_set_collect_cycle(const ipc_cmd_request_t *req,
                                        uint8_t *resp_data, uint16_t *resp_data_len)
{
    fprintf(stderr, "[CMD][MOCK] set_collect_cycle dtu_id=%u param_len=%u\n",
            req->dtu_id, req->param_len);

    const char *resp = "{\"result\":1,\"message\":\"collect cycle set (mock)\"}";
    uint16_t resp_len = (uint16_t)strlen(resp);
    if (resp_len > *resp_data_len)
        resp_len = *resp_data_len;
    memcpy(resp_data, resp, resp_len);
    *resp_data_len = resp_len;

    return CMD_RESULT_OK;
}

/*
 * Mock: 处理 trigger_collect 命令。
 */
static uint8_t handle_trigger_collect(const ipc_cmd_request_t *req,
                                      uint8_t *resp_data, uint16_t *resp_data_len)
{
    fprintf(stderr, "[CMD][MOCK] trigger_collect dtu_id=%u\n", req->dtu_id);

    const char *resp = "{\"result\":1,\"message\":\"collect triggered (mock)\"}";
    uint16_t resp_len = (uint16_t)strlen(resp);
    if (resp_len > *resp_data_len)
        resp_len = *resp_data_len;
    memcpy(resp_data, resp, resp_len);
    *resp_data_len = resp_len;

    return CMD_RESULT_OK;
}

/*
 * Mock: 处理 reboot 命令。
 */
static uint8_t handle_reboot(const ipc_cmd_request_t *req,
                             uint8_t *resp_data, uint16_t *resp_data_len)
{
    fprintf(stderr, "[CMD][MOCK] reboot dtu_id=%u\n", req->dtu_id);

    const char *resp = "{\"result\":1,\"message\":\"reboot accepted (mock)\"}";
    uint16_t resp_len = (uint16_t)strlen(resp);
    if (resp_len > *resp_data_len)
        resp_len = *resp_data_len;
    memcpy(resp_data, resp, resp_len);
    *resp_data_len = resp_len;

    return CMD_RESULT_OK;
}

/* ── 公开接口 ── */

int sle_cmd_handler_init(void)
{
    fprintf(stderr, "[CMD][STATUS] sle_cmd_handler initialized (mock mode)\n");
    return 0;
}

void sle_cmd_handler_deinit(void)
{
    fprintf(stderr, "[CMD][STATUS] sle_cmd_handler deinitialized\n");
}

uint8_t sle_cmd_handler_process(const ipc_cmd_request_t *req,
                                uint8_t *resp_data, uint16_t *resp_data_len)
{
    if (!req) {
        *resp_data_len = 0;
        return CMD_RESULT_FAILED;
    }

    fprintf(stderr, "[CMD][PROCESS] dtu_id=%u method=%u seq=%u\n",
            req->dtu_id, req->method, req->seq);

    switch (req->method) {
    case CMD_METHOD_SET_RELAY:
        return handle_set_relay(req, resp_data, resp_data_len);
    case CMD_METHOD_SET_MODE:
        return handle_set_mode(req, resp_data, resp_data_len);
    case CMD_METHOD_SET_COLLECT_CYCLE:
        return handle_set_collect_cycle(req, resp_data, resp_data_len);
    case CMD_METHOD_TRIGGER_COLLECT:
        return handle_trigger_collect(req, resp_data, resp_data_len);
    case CMD_METHOD_REBOOT:
        return handle_reboot(req, resp_data, resp_data_len);
    default:
        fprintf(stderr, "[CMD][WARN] unsupported method: %u\n", req->method);
        *resp_data_len = 0;
        return CMD_RESULT_UNSUPPORTED;
    }
}
