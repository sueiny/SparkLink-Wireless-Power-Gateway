#ifndef DTU_LOG_H
#define DTU_LOG_H

#include <stdint.h>

#include "dtu_types.h"
#include "errcode.h"

void dtu_log_info(const char *fmt, ...);
void dtu_log_error(const char *fmt, ...);
void dtu_log_transport(const char *transport, const char *fmt, ...);
void dtu_log_boot(errcode_t load_ret);
void dtu_log_commit(void);
void dtu_log_factory_reset(void);
void dtu_log_cfg_read_dev_info(void);
void dtu_log_cfg_read_uart(const dtu_uart_cfg_t *cfg);
void dtu_log_cfg_read_modbus(void);
void dtu_log_cfg_read_whitelist(void);
void dtu_log_cfg_read_power(uint8_t power);
void dtu_log_cfg_read_wl_node(const dtu_wl_item_t *item);
void dtu_log_cfg_write_role(uint8_t role);
void dtu_log_cfg_write_uart(const dtu_uart_cfg_t *cfg);
void dtu_log_cfg_write_modbus(void);
void dtu_log_cfg_write_power(uint8_t power);
void dtu_log_cfg_write_whitelist(void);
void dtu_log_cfg_write_wl_node(const dtu_wl_item_t *item);
void dtu_log_cfg_reject(dtu_transport_id_t transport_id, uint8_t cmd);
void dtu_log_run_forward(dtu_transport_id_t src, dtu_transport_id_t dst, uint16_t payload_len, uint16_t packet_len);

#endif
