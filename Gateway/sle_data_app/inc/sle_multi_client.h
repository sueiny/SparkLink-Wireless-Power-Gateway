#ifndef GATEWAY_SLE_MULTI_CLIENT_H
#define GATEWAY_SLE_MULTI_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include "sle_app_config.h"

/* SLE manager: 管理 SLE 协议栈、扫描、一对多连接状态机和连接维护 tick。 */
int sle_manager_init(const sle_app_config_t *config);

/* 停止扫描、注销 SSAP client 并关闭 SLE 协议栈。 */
void sle_manager_deinit(void);

/* 由维护线程周期调用，处理连接流程超时和 stale 检测。 */
void sle_manager_tick(void);

/* 返回 SLE client 是否仍处于运行状态。 */
bool sle_manager_is_running(void);

/* 将 gatewayd 已封装好的 ST 帧写入目标 root 的 SLE data property。 */
int sle_manager_write_st_frame(uint16_t root_id, const uint8_t *data, uint16_t len);

#endif
