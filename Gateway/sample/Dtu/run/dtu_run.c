#include "dtu_run.h"

#include "dtu_log.h"
#include "dtu_service_internal.h"
#include "dtu_storage.h"
#include "dtu_transport.h"
#include "securec.h"

/* 运行模式职责：
 * 1. 承接 RUN 模式下的业务数据流，而不是处理 DTU 配置协议命令。
 * 2. 当前阶段做透明桥接：SLE / UART0 调试口 / UART1(485) 原样互转。
 * 3. 后续组网协议接入时，只替换本文件中的 decode / encode / route 逻辑。
 *
 * 不负责：
 * 1. 不解析 AA55 配置协议帧。
 * 2. 不读写 NV 配置。
 * 3. 不维护配置命令拒配表。
 */

/* 当前透明测试只做有效性检查并原样透传；后续组网可替换 decode/encode。 */

typedef struct {
    const uint8_t *data;
    uint16_t len;
} dtu_run_payload_t;

/* RUN 透明 decode：当前不改 payload，只确认输入有效。 */
static bool dtu_run_decode_transparent(const uint8_t *data, uint16_t len, dtu_run_payload_t *payload)
{
    if (data == NULL || len == 0 || payload == NULL) {
        return false;
    }

    payload->data = data;
    payload->len = len;
    return true;
}

/* RUN 透明 encode：当前输出与输入完全一致。 */
static bool dtu_run_encode_transparent(const dtu_run_payload_t *payload, dtu_run_payload_t *encoded)
{
    if (payload == NULL || encoded == NULL || payload->data == NULL || payload->len == 0) {
        return false;
    }

    *encoded = *payload;
    return true;
}

/* 不用 %s 打印业务数据，避免二进制 payload 越界或污染串口。 */

/* 将 RUN 方向和原始 payload 打到 UART0，方便 PC 侧观察透明链路。 */
static void dtu_run_print_to_pc(const char *direction, const uint8_t *data, uint16_t len)
{
    char prefix[56] = {0};
    int ret;

    if (direction == NULL || data == NULL || len == 0) {
        return;
    }

    ret = snprintf_s(prefix, sizeof(prefix), sizeof(prefix) - 1, "\r\n[DTU RUN] %s len=%u: ", direction, len);
    if (ret > 0) {
        (void)dtu_uart_send_to_pc((const uint8_t *)prefix, (uint16_t)ret); // 先输出方向和长度，方便 PC 侧观察链路。
    }
    (void)dtu_uart_send_to_pc(data, len); // 再原样输出透明业务 payload。
    (void)dtu_uart_send_to_pc((const uint8_t *)"\r\n", 2); // 每包独立换行，避免串口日志粘连。
}

/* 发送 RUN payload 到 SLE client。 */
static void dtu_run_send_to_sle(const uint8_t *data, uint16_t len)
{
    const dtu_transport_if_t *sle_if = dtu_service_transport_if(DTU_TRANSPORT_SLE);
    errcode_t ret;

    if (sle_if == NULL || sle_if->send == NULL) {
        dtu_log_error("run forward failed: SLE transport unsupported");
        return;
    }

    ret = sle_if->send(data, len); // 通过 SLE notify/indicate 上行给 client。
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("run forward send failed: transport=%s ret=0x%x",
            dtu_log_transport_name(DTU_TRANSPORT_SLE), ret);
    }
}

/* 发送 RUN payload 到 UART1/485 总线。 */
static void dtu_run_send_to_485(const uint8_t *data, uint16_t len)
{
    errcode_t ret = dtu_uart_send_to_485(data, len); // 下行到 UART1/485 外设总线。

    if (ret != ERRCODE_SUCC) {
        dtu_log_error("run forward send failed: transport=UART485 ret=0x%x", ret);
    }
}

/* manager 只按来源转入这些入口；后续组网逻辑在入口内部扩展。 */

/* SLE 下发业务数据：打印到 UART0 观察口，并转发到 UART1/485。 */
void dtu_run_on_sle(const uint8_t *data, uint16_t len)
{
    dtu_run_payload_t payload;
    dtu_run_payload_t encoded;

    if (!dtu_run_decode_transparent(data, len, &payload) ||
        !dtu_run_encode_transparent(&payload, &encoded)) {
        return;
    }

    dtu_log_run_forward(DTU_TRANSPORT_SLE, DTU_TRANSPORT_UART, payload.len, encoded.len); // trace 打开时记录 SLE 下行摘要。
    dtu_run_print_to_pc("RUN SLE->485", encoded.data, encoded.len); // 镜像到 UART0 方便现场观察。
    dtu_run_send_to_485(encoded.data, encoded.len); // SLE 下行透明转发到 485。
}

/* UART0 PC 调试输入：转发到 UART1/485，并镜像给 SLE client 方便联调观察。 */
void dtu_run_on_uart0(const uint8_t *data, uint16_t len)
{
    dtu_run_payload_t payload;
    dtu_run_payload_t encoded;

    if (!dtu_run_decode_transparent(data, len, &payload) ||
        !dtu_run_encode_transparent(&payload, &encoded)) {
        return;
    }

    dtu_log_run_forward(DTU_TRANSPORT_UART, DTU_TRANSPORT_UART, payload.len, encoded.len); // trace 打开时记录 PC 调试输入。
    dtu_run_send_to_485(encoded.data, encoded.len); // UART0 调试输入转发到 485。
    dtu_run_send_to_sle(encoded.data, encoded.len); // 同步镜像到 SLE client，方便联调观察。
}

/* UART1/485 返回数据：打印到 UART0 观察口，并转发给 SLE client。 */
void dtu_run_on_485(const uint8_t *data, uint16_t len)
{
    dtu_run_payload_t payload;
    dtu_run_payload_t encoded;

    if (dtu_storage_current_mode() != DTU_MODE_RUN) { // CONFIG 模式不处理 485 业务回包。
        return;
    }
    if (!dtu_run_decode_transparent(data, len, &payload) ||
        !dtu_run_encode_transparent(&payload, &encoded)) {
        return;
    }

    dtu_log_run_forward(DTU_TRANSPORT_UART, DTU_TRANSPORT_SLE, payload.len, encoded.len); // trace 打开时记录 485 上行摘要。
    dtu_run_print_to_pc("RUN 485->SLE", encoded.data, encoded.len); // 485 回包同步打印到 UART0。
    dtu_run_send_to_sle(encoded.data, encoded.len); // 485 上行透明转发给 SLE client。
}
