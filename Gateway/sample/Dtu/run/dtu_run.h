#ifndef DTU_RUN_H
#define DTU_RUN_H

#include <stdint.h>

void dtu_run_on_sle(const uint8_t *data, uint16_t len);
void dtu_run_on_uart0(const uint8_t *data, uint16_t len);
void dtu_run_on_485(const uint8_t *data, uint16_t len);

#endif
