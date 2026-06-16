/*
 * sle_cmd_handler.c - gatewayd raw ST downlink bridge.
 *
 * Boundary:
 *   gatewayd owns device model, command validation, Modbus, and ST framing.
 *   sle_data_app only validates the transport frame and writes bytes to SLE.
 */

#include "sle_cmd_handler.h"

#include "sle_multi_client.h"

#include <stdio.h>
#include <string.h>

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint16_t write_json_response(uint8_t *resp_data,
                                    uint16_t *resp_data_len,
                                    const char *message,
                                    uint16_t root_id,
                                    uint16_t dst_node_id,
                                    uint16_t st_len)
{
    char resp[160];
    int n = snprintf(resp, sizeof(resp),
        "{\"result\":1,\"message\":\"%s\",\"root_id\":%u,\"dst_node_id\":%u,\"st_len\":%u}",
        message, root_id, dst_node_id, st_len);
    if (n < 0) {
        *resp_data_len = 0;
        return 0;
    }

    uint16_t resp_len = (uint16_t)n;
    if (resp_len > *resp_data_len)
        resp_len = *resp_data_len;
    memcpy(resp_data, resp, resp_len);
    *resp_data_len = resp_len;
    return resp_len;
}

static uint8_t fail_response(uint8_t *resp_data,
                             uint16_t *resp_data_len,
                             const char *message)
{
    char resp[128];
    int n = snprintf(resp, sizeof(resp),
        "{\"result\":0,\"message\":\"%s\"}", message);
    if (n > 0) {
        uint16_t resp_len = (uint16_t)n;
        if (resp_len > *resp_data_len)
            resp_len = *resp_data_len;
        memcpy(resp_data, resp, resp_len);
        *resp_data_len = resp_len;
    } else {
        *resp_data_len = 0;
    }
    return CMD_RESULT_FAILED;
}

static uint8_t handle_raw_st_downlink(const ipc_cmd_request_t *req,
                                      uint8_t *resp_data,
                                      uint16_t *resp_data_len)
{
    if (req->param_len < IPC_CMD_RAW_ST_META_LEN) {
        fprintf(stderr, "[CMD][ST-RX][WARN] raw ST param too short len=%u\n", req->param_len);
        return fail_response(resp_data, resp_data_len, "raw ST param too short");
    }

    const uint8_t *param = req->param_data;
    uint16_t root_id = read_u16_le(param);
    uint16_t dst_node_id = read_u16_le(param + 2);
    uint16_t st_len = read_u16_le(param + 4);

    if (st_len == 0 || st_len > IPC_CMD_MAX_ST_FRAME_LEN ||
        req->param_len != IPC_CMD_RAW_ST_META_LEN + st_len) {
        fprintf(stderr, "[CMD][ST-RX][WARN] invalid raw ST len root_id=%u dst_node_id=%u st_len=%u param_len=%u\n",
            root_id, dst_node_id, st_len, req->param_len);
        return fail_response(resp_data, resp_data_len, "invalid raw ST length");
    }

    const uint8_t *frame = param + IPC_CMD_RAW_ST_META_LEN;
    if (st_len < 13 || frame[0] != 'S' || frame[1] != 'T' || frame[2] != 0x01) {
        fprintf(stderr, "[CMD][ST-RX][WARN] invalid ST header root_id=%u dst_node_id=%u st_len=%u\n",
            root_id, dst_node_id, st_len);
        return fail_response(resp_data, resp_data_len, "invalid ST header");
    }

    uint16_t frame_dst = read_u16_le(frame + 7);
    if (frame_dst != dst_node_id) {
        fprintf(stderr, "[CMD][ST-RX][WARN] ST dst mismatch meta=%u frame=%u\n",
            dst_node_id, frame_dst);
        return fail_response(resp_data, resp_data_len, "ST dst mismatch");
    }

    fprintf(stderr, "[CMD][ST-RX] seq=%u root_id=%u dst_node_id=%u st_len=%u\n",
        req->seq, root_id, dst_node_id, st_len);

    int ret = sle_manager_write_st_frame(root_id, frame, st_len);
    if (ret != 0) {
        fprintf(stderr, "[CMD][ST-RX][WARN] raw ST forward failed ret=%d root_id=%u dst_node_id=%u\n",
            ret, root_id, dst_node_id);
        return fail_response(resp_data, resp_data_len, "raw ST forward failed");
    }

    write_json_response(resp_data, resp_data_len, "raw ST forwarded", root_id, dst_node_id, st_len);
    return CMD_RESULT_OK;
}

int sle_cmd_handler_init(void)
{
    fprintf(stderr, "[CMD][STATUS] sle_cmd_handler initialized (raw ST bridge)\n");
    return 0;
}

void sle_cmd_handler_deinit(void)
{
    fprintf(stderr, "[CMD][STATUS] sle_cmd_handler deinitialized\n");
}

uint8_t sle_cmd_handler_process(const ipc_cmd_request_t *req,
                                uint8_t *resp_data,
                                uint16_t *resp_data_len)
{
    if (!req || !resp_data || !resp_data_len) {
        return CMD_RESULT_FAILED;
    }

    fprintf(stderr, "[CMD][PROCESS] dtu_id=%u method=%u seq=%u param_len=%u\n",
            req->dtu_id, req->method, req->seq, req->param_len);

    if (req->method == CMD_METHOD_RAW_ST_DOWNLINK)
        return handle_raw_st_downlink(req, resp_data, resp_data_len);

    fprintf(stderr, "[CMD][WARN] unsupported non-raw method: %u\n", req->method);
    *resp_data_len = 0;
    return CMD_RESULT_UNSUPPORTED;
}
