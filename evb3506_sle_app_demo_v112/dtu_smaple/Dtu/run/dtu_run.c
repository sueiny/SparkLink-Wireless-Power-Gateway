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

/* ========================================================================== */
/* RUN payload 编解码区                                                       */
/* 说明：                                                                     */
/* 1. 当前透明测试只做基本有效性检查，然后原样透传。                          */
/* 2. 后续组网时，在 decode 中拆包/校验/提取节点信息。                        */
/* 3. 后续组网时，在 encode 中补节点信息/组包/封装 SLE 或 485 业务帧。         */
/* ========================================================================== */

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

/* ========================================================================== */
/* RUN 输出辅助区                                                             */
/* 说明：                                                                     */
/* 1. UART0 是 RUN 模式观察口，只打印方向和原始字节。                         */
/* 2. SLE 和 UART1/485 发送失败统一打错误日志。                               */
/* 3. 不使用 %s 打印业务数据，避免二进制 payload 越界或乱码污染。             */
/* ========================================================================== */

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
        (void)dtu_uart_send_to_pc((const uint8_t *)prefix, (uint16_t)ret);
    }
    (void)dtu_uart_send_to_pc(data, len);
    (void)dtu_uart_send_to_pc((const uint8_t *)"\r\n", 2);
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

    ret = sle_if->send(data, len);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("run forward send failed: transport=%s ret=0x%x",
            dtu_service_transport_name(DTU_TRANSPORT_SLE), ret);
    }
}

/* 发送 RUN payload 到 UART1/485 总线。 */
static void dtu_run_send_to_485(const uint8_t *data, uint16_t len)
{
    errcode_t ret = dtu_uart_send_to_485(data, len);

    if (ret != ERRCODE_SUCC) {
        dtu_log_error("run forward send failed: transport=UART485 ret=0x%x", ret);
    }
}

/* ========================================================================== */
/* RUN 业务入口区                                                             */
/* 说明：                                                                     */
/* 1. manager 只负责按来源把数据转进这些入口。                                */
/* 2. UART transport 的 485 子通道也只调用 on_485，不参与公共 transport 枚举。 */
/* 3. 当前透明测试保持最短路径，后续组网只在这些入口内部扩展。                */
/* ========================================================================== */

/* SLE 下发业务数据：打印到 UART0 观察口，并转发到 UART1/485。 */
void dtu_run_on_sle(const uint8_t *data, uint16_t len)
{
    dtu_run_payload_t payload;
    dtu_run_payload_t encoded;

    if (!dtu_run_decode_transparent(data, len, &payload) ||
        !dtu_run_encode_transparent(&payload, &encoded)) {
        return;
    }

    dtu_log_run_forward(DTU_TRANSPORT_SLE, DTU_TRANSPORT_UART, payload.len, encoded.len);
    dtu_run_print_to_pc("RUN SLE->485", encoded.data, encoded.len);
    dtu_run_send_to_485(encoded.data, encoded.len);
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

    dtu_log_run_forward(DTU_TRANSPORT_UART, DTU_TRANSPORT_UART, payload.len, encoded.len);
    dtu_run_send_to_485(encoded.data, encoded.len);
    dtu_run_send_to_sle(encoded.data, encoded.len);
}

/* UART1/485 返回数据：打印到 UART0 观察口，并转发给 SLE client。 */
void dtu_run_on_485(const uint8_t *data, uint16_t len)
{
    dtu_run_payload_t payload;
    dtu_run_payload_t encoded;

    if (dtu_storage_current_mode() != DTU_MODE_RUN) {
        return;
    }
    if (!dtu_run_decode_transparent(data, len, &payload) ||
        !dtu_run_encode_transparent(&payload, &encoded)) {
        return;
    }

    dtu_log_run_forward(DTU_TRANSPORT_UART, DTU_TRANSPORT_SLE, payload.len, encoded.len);
    dtu_run_print_to_pc("RUN 485->SLE", encoded.data, encoded.len);
    dtu_run_send_to_sle(encoded.data, encoded.len);
}
