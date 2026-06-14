#ifndef GATEWAY_SLE_MOCK_DATA_GENERATOR_H
#define GATEWAY_SLE_MOCK_DATA_GENERATOR_H

/*
 * 独立的模拟数据生成器线程。
 * 在 SLE 硬件不可用时，生成模拟数据并通过 IPC 发送到 gatewayd。
 */

/* 初始化模拟数据生成器 */
int mock_data_generator_init(void);

/* 启动模拟数据生成器线程 */
int mock_data_generator_start(void);

/* 停止模拟数据生成器线程 */
void mock_data_generator_stop(void);

#endif
