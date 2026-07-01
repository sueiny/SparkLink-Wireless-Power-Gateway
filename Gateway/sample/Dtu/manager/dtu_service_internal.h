/**
 * @file dtu_service_internal.h
 * @brief manager 内部接口，外部模块应使用 dtu_service.h。
 */

#ifndef DTU_SERVICE_INTERNAL_H
#define DTU_SERVICE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "dtu_transport.h"

/** 返回 transport 接口对象；未注册时返回 NULL。 */
const dtu_transport_if_t *dtu_service_transport_if(dtu_transport_id_t transport_id);

/** 通过指定 transport 发送完整配置协议响应。 */
void dtu_service_reply(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq,
    const uint8_t *body, uint16_t body_len);

/** 发送仅包含状态码的配置协议响应。 */
void dtu_service_reply_status(dtu_transport_id_t transport_id, uint8_t cmd, uint8_t seq, uint8_t status);

#endif
