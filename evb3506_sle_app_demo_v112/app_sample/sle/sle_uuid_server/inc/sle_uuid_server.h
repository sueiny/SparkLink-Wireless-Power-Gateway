/**
 * Copyright (c) @CompanyNameMagicTag 2023-2023. All rights reserved.
 * Description: SLE uart server Config.
 */

#ifndef SLE_UART_SERVER_H
#define SLE_UART_SERVER_H

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#define BYTE_LEN_128 128

#define SUPPORT_SPEED_TEST TRUE

#include <stdint.h>
#include "sle_ssap_server.h"
#include "errcode.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* Service UUID */
#define SLE_UUID_SERVER_SERVICE        0x060B

/* RX Property UUID */
#define SLE_UUID_SERVER_RX     0x2323

/* Property permission */
#define SLE_UUID_RX_PERMISSIONS  (SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE)

/* Operation indication */
#define SLE_UUID_RX_OPERATION_INDICATION  (SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP)

/* Descriptor Property */
#define SLE_UUID_RX_DESCRIPTOR_OPERATION_INDICATION (SSAP_OPERATE_INDICATION_BIT_DESCRIPTOR_CLIENT_CONFIGURATION_WRITE)

/* TX Property UUID */
#define SLE_UUID_SERVER_TX     0x2310

/* Property permission */
#define SLE_UUID_TX_PERMISSIONS  (SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE)

/* Operation indication */
#define SLE_UUID_TX_OPERATION_INDICATION  (SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_NOTIFY \
                                          | SSAP_OPERATE_INDICATION_BIT_INDICATE)

/* Descriptor Property */
#define SLE_UUID_TX_DESCRIPTOR_OPERATION_INDICATION (SSAP_OPERATE_INDICATION_BIT_DESCRIPTOR_CLIENT_CONFIGURATION_WRITE)
#define SLE_UUID_CONFIGURATION 0x2902

errcode_t sle_uuid_server_init(void);

errcode_t sle_uuid_server_deinit(void);

errcode_t sle_uuid_server_send_report_by_handle(uint16_t handle, uint8_t *data, uint8_t len);

errcode_t sle_enable_server_cbk(void);

void sle_wait_result_handle(uint8_t timeout, uint8_t *wait_flag, uint8_t wait_value);

void sle_enable_cbk(errcode_t status);

void sle_disable_cbk(errcode_t status);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif