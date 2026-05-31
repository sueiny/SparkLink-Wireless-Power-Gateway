#ifndef DTU_BOARD_H
#define DTU_BOARD_H

#include "dtu_types.h"
#include "errcode.h"

/* 初始化 DIP 和两组三色灯，并按当前模式/角色刷新状态灯。 */
errcode_t dtu_board_init(void);
/* 读取 GPIO13 DIP：高电平 CONFIG，低电平 RUN。 */
dtu_mode_t dtu_board_detect_mode(void);
/* UART/SLE/485 有数据活动时调用，活动灯闪烁提示链路活动。 */
void dtu_board_mark_data_activity(void);

#endif
