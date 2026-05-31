/**
 * @file dtu_channel_uart.c
 * @brief DTU UART传输通道实现
 * @details 本模块实现了DTU系统的UART传输通道，包括：
 *          - UART0：公共配置口/PC调试口，CONFIG下进入配置协议，RUN下进入运行态调试输入
 *          - UART1/485：485总线口，只在RUN模式初始化，收到485数据后交给manager/RUN
 *
 * @version 2.0
 * @date 2026-05-20
 *
 * @par 架构说明：
 *       - 每路UART用独立context管理资源，但业务入口仍显式区分UART0和UART1
 *       - UART0始终初始化，用于配置和调试
 *       - UART1/485只在RUN模式初始化，用于485总线通信
 *
 * @par 数据流转：
 *       - 接收：UART RX callback -> ring buffer -> transport task -> manager
 *       - 发送：manager -> transport send -> UART DMA发送
 *
 * @par 缓冲区设计：
 *       - driver buffer：UART驱动内部RX缓冲，由硬件DMA使用
 *       - ring buffer：DTU自己的跨callback/task缓冲，用于异步数据处理
 *       - 批处理：transport task每次从ring取出batch_size字节进行处理
 */

#include "dtu_transport.h"

#include "dma.h"
#include "dtu_log.h"
#include "dtu_service.h"
#include "dtu_storage.h"
#include "hal_dma.h"
#include "hal_reboot.h"
#include "osal_debug.h"
#include "pinctrl.h"
#include "soc_osal.h"

/* UART 通道职责：
 * 1. UART0：公共配置口/PC 调试口，CONFIG 下进入配置协议，RUN 下进入运行态调试输入。
 * 2. UART1：485 总线口，只在 RUN 模式初始化，收到 485 数据后交给 manager/RUN。
 * 3. 每路 UART 用独立 context 管理资源，但业务入口仍显式区分 UART0 和 UART1。
 */

/**
 * @brief UART通道上下文结构体
 * @details 管理单个UART通道的所有资源，包括：
 *          - 硬件配置：总线号、引脚、引脚模式
 *          - 任务配置：任务名、配置参数
 *          - 缓冲区：驱动缓冲、环形缓冲
 *          - 同步对象：信号量
 *
 * @par 每个UART通道都有独立的context，互不干扰
 */
typedef struct {
    uart_bus_t bus;                    /**< UART总线号 */
    pin_t tx_pin;                      /**< 发送引脚 */
    pin_t rx_pin;                      /**< 接收引脚 */
    pin_mode_t pin_mode;               /**< 引脚模式 */
    const char *name;                  /**< 通道名称，用于日志 */
    const char *task_name;             /**< 处理任务名称 */
    dtu_uart_cfg_t fixed_cfg;          /**< 固定配置（UART0使用） */
    bool use_runtime_cfg;              /**< 是否使用运行时配置（UART1/485使用） */
    uint8_t rx_driver_buffer[DTU_CFG_RX_DRIVER_BUFFER_SIZE]; /**< 驱动接收缓冲 */
    uart_buffer_config_t buffer_cfg;   /**< 驱动缓冲配置 */
    uart_write_dma_config_t dma_cfg;   /**< DMA发送配置 */
    uint8_t rx_ring[DTU_CFG_RING_BUFFER_SIZE]; /**< 环形接收缓冲 */
    volatile uint16_t rx_head;         /**< 环形缓冲写指针 */
    volatile uint16_t rx_tail;         /**< 环形缓冲读指针 */
    osal_semaphore rx_sem;             /**< 接收信号量 */
} dtu_uart_ctx_t;

/**
 * @brief UART0通道上下文
 * @details UART0用于配置口/PC调试口，使用固定配置
 *
 * @par 配置说明：
 *       - 使用DTU_CFG_UART_BUS总线
 *       - 使用DTU_CFG_UART_TX_PIN和DTU_CFG_UART_RX_PIN引脚
 *       - 使用固定配置DTU_CFG_UART0_DEFAULT_CFG_INIT
 *       - 不使用运行时配置
 */
static dtu_uart_ctx_t g_uart0_ctx = {
    .bus = DTU_CFG_UART_BUS,
    .tx_pin = DTU_CFG_UART_TX_PIN,
    .rx_pin = DTU_CFG_UART_RX_PIN,
    .pin_mode = DTU_CFG_UART_PIN_MODE,
    .name = "UART0",
    .task_name = DTU_CFG_UART0_TASK_NAME,
    .fixed_cfg = DTU_CFG_UART0_DEFAULT_CFG_INIT,
    .use_runtime_cfg = false,
    .buffer_cfg = {
        .rx_buffer = NULL,
        .rx_buffer_size = DTU_CFG_RX_DRIVER_BUFFER_SIZE
    },
    .dma_cfg = {
        .src_width = HAL_DMA_TRANSFER_WIDTH_8,
        .dest_width = HAL_DMA_TRANSFER_WIDTH_8,
        .burst_length = HAL_DMA_BURST_TRANSACTION_LENGTH_1,
        .priority = HAL_DMA_CH_PRIORITY_0
    },
    .rx_head = 0,
    .rx_tail = 0,
    .rx_sem = { 0 }
};

/**
 * @brief UART1/485通道上下文
 * @details UART1/485用于RUN模式下的485总线通信，使用运行时配置
 *
 * @par 配置说明：
 *       - 使用DTU_CFG_485_UART_BUS总线
 *       - 使用DTU_CFG_485_UART_TX_PIN和DTU_CFG_485_UART_RX_PIN引脚
 *       - 使用运行时配置（从NV加载）
 *       - 首次烧录、NV无效、恢复出厂时使用默认值
 */
static dtu_uart_ctx_t g_uart1_485_ctx = {
    .bus = DTU_CFG_485_UART_BUS,
    .tx_pin = DTU_CFG_485_UART_TX_PIN,
    .rx_pin = DTU_CFG_485_UART_RX_PIN,
    .pin_mode = DTU_CFG_485_UART_PIN_MODE,
    .name = "UART1/485",
    .task_name = DTU_CFG_485_TASK_NAME,
    .fixed_cfg = DTU_CFG_485_DEFAULT_CFG_INIT,
    .use_runtime_cfg = true,
    .buffer_cfg = {
        .rx_buffer = NULL,
        .rx_buffer_size = DTU_CFG_RX_DRIVER_BUFFER_SIZE
    },
    .dma_cfg = {
        .src_width = HAL_DMA_TRANSFER_WIDTH_8,
        .dest_width = HAL_DMA_TRANSFER_WIDTH_8,
        .burst_length = HAL_DMA_BURST_TRANSACTION_LENGTH_1,
        .priority = HAL_DMA_CH_PRIORITY_0
    },
    .rx_head = 0,
    .rx_tail = 0,
    .rx_sem = { 0 }
};

/* ========================================================================== */
/* context ring 操作区                                                        */
/* @brief 环形缓冲区操作函数                                                  */
/* @details 这些函数实现了环形缓冲区的基本操作，用于UART接收数据的缓冲        */
/* ========================================================================== */

/**
 * @brief 获取环形缓冲区已使用大小
 * @details 计算环形缓冲区中已存储的数据量
 *
 * @param[in] ctx UART通道上下文
 * @return 已使用的字节数
 *
 * @note 使用volatile修饰的head和tail指针，确保多线程安全
 */
static uint16_t dtu_uart_ring_used(const dtu_uart_ctx_t *ctx)
{
    if (ctx->rx_head >= ctx->rx_tail) {
        return (uint16_t)(ctx->rx_head - ctx->rx_tail);
    }
    return (uint16_t)(DTU_CFG_RING_BUFFER_SIZE - ctx->rx_tail + ctx->rx_head);
}

/**
 * @brief 向环形缓冲区写入一个字节
 * @details 将一个字节写入环形缓冲区，如果缓冲区满则返回false
 *
 * @param[in] ctx UART通道上下文
 * @param[in] byte 要写入的字节
 * @return true: 写入成功, false: 缓冲区已满
 *
 * @note 该函数在UART RX callback中调用，需要快速执行
 */
static bool dtu_uart_ring_push(dtu_uart_ctx_t *ctx, uint8_t byte)
{
    uint16_t next = (uint16_t)((ctx->rx_head + 1) % DTU_CFG_RING_BUFFER_SIZE);

    if (next == ctx->rx_tail) {
        return false;
    }
    ctx->rx_ring[ctx->rx_head] = byte;
    ctx->rx_head = next;
    return true;
}

/**
 * @brief 从环形缓冲区读取一个字节
 * @details 从环形缓冲区读取一个字节，如果缓冲区空则返回false
 *
 * @param[in] ctx UART通道上下文
 * @param[out] byte 读取的字节存储位置
 * @return true: 读取成功, false: 缓冲区为空
 *
 * @note 该函数在transport task中调用，用于批量读取数据
 */
static bool dtu_uart_ring_pop(dtu_uart_ctx_t *ctx, uint8_t *byte)
{
    if (ctx->rx_tail == ctx->rx_head) {
        return false;
    }
    *byte = ctx->rx_ring[ctx->rx_tail];
    ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1) % DTU_CFG_RING_BUFFER_SIZE);
    return true;
}

/* ========================================================================== */
/* 接收 callback 区                                                           */
/* @brief UART接收回调函数                                                    */
/* @details 这些函数在UART接收到数据时被调用，将数据从驱动缓冲搬运到ring缓冲  */
/* ========================================================================== */

/**
 * @brief 共用callback搬运函数
 * @details 将UART接收到的数据从驱动缓冲搬运到对应context的ring缓冲
 *
 * @param[in] ctx UART通道上下文
 * @param[in] buffer 接收到的数据缓冲
 * @param[in] length 数据长度
 * @param[in] error 是否有硬件错误
 *
 * @note 该函数只做数据搬运，不做业务处理
 *       业务处理在transport task中进行
 */
static void dtu_uart_rx_to_context(dtu_uart_ctx_t *ctx, const void *buffer, uint16_t length, bool error)
{
    const uint8_t *data = (const uint8_t *)buffer;
    uint16_t accepted = 0;

    if (ctx == NULL || data == NULL || length == 0) {
        return;
    }
    if (error) {
        dtu_log_error("%s rx hardware error, drop len=%u", ctx->name, length);
        return;
    }

    for (uint16_t i = 0; i < length; i++) {
        if (!dtu_uart_ring_push(ctx, data[i])) {
            break;
        }
        accepted++;
    }

    dtu_service_trace_rx_batch(length, accepted, dtu_uart_ring_used(ctx));
    osal_sem_up(&ctx->rx_sem);
}

/**
 * @brief UART0 RX callback
 * @details UART0接收到数据时的回调函数，只搬运到UART0 context
 *
 * @param[in] buffer 接收到的数据缓冲
 * @param[in] length 数据长度
 * @param[in] error 是否有硬件错误
 */
static void dtu_uart0_rx_callback(const void *buffer, uint16_t length, bool error)
{
    dtu_uart_rx_to_context(&g_uart0_ctx, buffer, length, error);
}

/**
 * @brief UART1/485 RX callback
 * @details UART1/485接收到数据时的回调函数，只搬运到UART1/485 context
 *
 * @param[in] buffer 接收到的数据缓冲
 * @param[in] length 数据长度
 * @param[in] error 是否有硬件错误
 */
static void dtu_uart1_485_rx_callback(const void *buffer, uint16_t length, bool error)
{
    dtu_uart_rx_to_context(&g_uart1_485_ctx, buffer, length, error);
}

/* ========================================================================== */
/* 接收批量辅助区                                                             */
/* @brief 批量数据读取函数                                                    */
/* @details 这些函数用于从ring缓冲区批量读取数据，提高处理效率                */
/* ========================================================================== */

/**
 * @brief 从ring缓冲区批量读取数据
 * @details 从ring缓冲区读取指定数量的字节，实际读取数量可能小于请求值
 *
 * @param[in] ctx UART通道上下文
 * @param[out] batch 读取数据存储缓冲
 * @param[in] batch_size 请求读取的字节数
 * @return 实际读取的字节数
 *
 * @note 该函数在transport task中调用，用于批量处理接收数据
 */
static uint16_t dtu_uart_pop_batch(dtu_uart_ctx_t *ctx, uint8_t *batch, uint16_t batch_size)
{
    uint16_t count = 0;

    if (ctx == NULL || batch == NULL || batch_size == 0) {
        return 0;
    }

    while (count < batch_size && dtu_uart_ring_pop(ctx, &batch[count])) {
        count++;
    }
    return count;
}

/* ========================================================================== */
/* 初始化与发送辅助区                                                         */
/* @brief UART初始化和发送辅助函数                                            */
/* @details 这些函数用于UART通道的初始化配置和数据发送                        */
/* ========================================================================== */

/**
 * @brief 获取UART有效配置
 * @details 根据UART通道配置返回有效的串口配置参数
 *
 * @param[in] ctx UART通道上下文
 * @return 有效配置指针，失败返回NULL
 *
 * @note UART0使用固定配置，UART1/485使用运行时配置
 */
static const dtu_uart_cfg_t *dtu_uart_effective_cfg(const dtu_uart_ctx_t *ctx)
{
    if (ctx != NULL && ctx->use_runtime_cfg) {
        return &dtu_storage_runtime_const()->uart_cfg;
    }
    return (ctx == NULL) ? NULL : &ctx->fixed_cfg;
}

/**
 * @brief 初始化UART通道上下文
 * @details 配置UART硬件参数，注册接收回调，创建处理任务
 *
 * @param[in] ctx UART通道上下文
 * @param[in] rx_callback 接收回调函数
 * @param[in] task_handler 处理任务函数
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @par 初始化流程：
 *       1. 获取有效配置参数
 *       2. 配置UART引脚和参数
 *       3. 初始化信号量
 *       4. 初始化UART驱动
 *       5. 注册接收回调
 *       6. 创建处理任务
 */
static errcode_t dtu_uart_init_context(dtu_uart_ctx_t *ctx, uart_rx_callback_t rx_callback,
    osal_kthread_handler task_handler)
{
    const dtu_uart_cfg_t *effective_cfg;
    uart_attr_t uart_attr;
    uart_extra_attr_t extra_attr = {
        .tx_dma_enable = true,
        .tx_int_threshold = UART_FIFO_INT_TX_LEVEL_EQ_0_CHARACTER,
        .rx_dma_enable = false,
        .rx_int_threshold = UART_FIFO_INT_RX_LEVEL_1_CHARACTER
    };
    uart_pin_config_t uart_pins;
    osal_task *task;
    errcode_t ret;

    if (ctx == NULL || rx_callback == NULL || task_handler == NULL) {
        return ERRCODE_FAIL;
    }
    effective_cfg = dtu_uart_effective_cfg(ctx);
    if (effective_cfg == NULL) {
        return ERRCODE_FAIL;
    }

    uart_pins.tx_pin = ctx->tx_pin;
    uart_pins.rx_pin = ctx->rx_pin;
    uart_pins.cts_pin = PIN_NONE;
    uart_pins.rts_pin = PIN_NONE;

    ctx->buffer_cfg.rx_buffer = ctx->rx_driver_buffer;
    dtu_storage_fill_uart_attr(&uart_attr, effective_cfg);
    extra_attr.rx_int_threshold = dtu_storage_rx_int_threshold();

    uapi_pin_set_mode(ctx->tx_pin, ctx->pin_mode);
    uapi_pin_set_mode(ctx->rx_pin, ctx->pin_mode);
    uapi_uart_deinit(ctx->bus);

    ret = osal_sem_binary_sem_init(&ctx->rx_sem, 0);
    if (ret != OSAL_SUCCESS) {
        dtu_log_error("%s sem init failed: 0x%x", ctx->name, ret);
        return ERRCODE_FAIL;
    }

    ret = uapi_uart_init(ctx->bus, &uart_pins, &uart_attr, &extra_attr, &ctx->buffer_cfg);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("%s init failed: 0x%x", ctx->name, ret);
        return ret;
    }

    ret = uapi_uart_register_rx_callback(ctx->bus, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE,
        dtu_storage_rx_notify_length(), rx_callback);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("%s callback register failed: 0x%x", ctx->name, ret);
        return ret;
    }
    dtu_log_transport("UART", "%s init ret=0x0 bus=%u tx=%u rx=%u baud=%u parity=%s stop=%u data=%u",
        ctx->name, ctx->bus, ctx->tx_pin, ctx->rx_pin,
        dtu_storage_uart_baudrate(effective_cfg->baud_level),
        dtu_storage_parity_name(effective_cfg->parity),
        effective_cfg->stop_bits, effective_cfg->data_bits);

    task = osal_kthread_create(task_handler, NULL, ctx->task_name, DTU_CFG_TRANSPORT_TASK_STACK_SIZE);
    if (task == NULL) {
        return ERRCODE_FAIL;
    }
    osal_kthread_set_priority(task, DTU_CFG_TRANSPORT_TASK_PRIO);
    return ERRCODE_SUCC;
}

/**
 * @brief 通过UART发送数据
 * @details 使用DMA方式通过UART发送数据
 *
 * @param[in] ctx UART通道上下文
 * @param[in] data 要发送的数据指针
 * @param[in] len 数据长度
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @note 使用DMA发送，提高CPU效率
 */
static errcode_t dtu_uart_send_context(dtu_uart_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    int32_t ret;

    if (ctx == NULL || data == NULL || len == 0) {
        return ERRCODE_FAIL;
    }

    ret = uapi_uart_write_by_dma(ctx->bus, data, len, &ctx->dma_cfg);
#if defined(CONFIG_UART_USING_V151)
    return (ret == ERRCODE_SUCC) ? ERRCODE_SUCC : (errcode_t)ret;
#else
    return (ret == (int32_t)len) ? ERRCODE_SUCC : (errcode_t)ret;
#endif
}

/**
 * @brief UART0处理任务
 * @details 处理UART0接收到的数据，CONFIG模式走配置协议，RUN模式作为PC调试输入
 *
 * @param[in] arg 任务参数（未使用）
 * @return NULL
 *
 * @par 处理逻辑：
 *       1. 从ring缓冲区批量读取数据
 *       2. 如果有数据，调用dtu_service_on_bytes处理
 *       3. 如果没有数据，检查是否需要重启
 *       4. 等待信号量或超时后继续循环
 */
static void *dtu_uart0_task(const char *arg)
{
    uint8_t batch[DTU_CFG_TRANSPORT_RX_BATCH_SIZE];

    unused(arg);
    while (1) {
        uint16_t count = dtu_uart_pop_batch(&g_uart0_ctx, batch, sizeof(batch));

        if (count > 0) {
            dtu_service_on_bytes(DTU_TRANSPORT_UART, batch, count);
            continue;
        }

        if (dtu_storage_is_reboot_pending()) {
            osal_msleep(DTU_CFG_REBOOT_DELAY_MS);
            hal_reboot_chip();
        }

        dtu_service_trace_rx_task_wakeup();
        if (osal_sem_down(&g_uart0_ctx.rx_sem) != OSAL_SUCCESS) {
            osal_msleep(DTU_CFG_TASK_IDLE_RETRY_MS);
        }
    }

    return NULL;
}

/**
 * @brief UART1/485处理任务
 * @details 处理UART1/485接收到的数据，485设备返回数据先交给manager，再进入RUN业务层
 *
 * @param[in] arg 任务参数（未使用）
 * @return NULL
 *
 * @par 处理逻辑：
 *       1. 从ring缓冲区批量读取数据
 *       2. 如果有数据，调用dtu_service_on_uart485_bytes处理
 *       3. 如果没有数据，等待信号量或超时后继续循环
 */
static void *dtu_uart1_485_task(const char *arg)
{
    uint8_t batch[DTU_CFG_TRANSPORT_RX_BATCH_SIZE];

    unused(arg);
    while (1) {
        uint16_t count = dtu_uart_pop_batch(&g_uart1_485_ctx, batch, sizeof(batch));

        if (count > 0) {
            dtu_service_on_uart485_bytes(batch, count);
            continue;
        }

        dtu_service_trace_rx_task_wakeup();
        if (osal_sem_down(&g_uart1_485_ctx.rx_sem) != OSAL_SUCCESS) {
            osal_msleep(DTU_CFG_TASK_IDLE_RETRY_MS);
        }
    }

    return NULL;
}

/* ========================================================================== */
/* 对外接口区                                                                 */
/* @brief UART传输通道对外接口实现                                            */
/* @details 这些函数实现了dtu_transport.h中定义的接口                          */
/* ========================================================================== */

/**
 * @brief UART传输通道初始化
 * @details 初始化UART模块，包括DMA、UART0和UART1/485
 *
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @par 初始化顺序：
 *       1. 初始化DMA控制器
 *       2. 初始化UART0（总是初始化）
 *       3. 如果是RUN模式，初始化UART1/485
 */
static errcode_t dtu_uart_transport_init_impl(void)
{
    errcode_t ret;

    uapi_dma_init();
    uapi_dma_open();

    ret = dtu_uart_init_context(&g_uart0_ctx, dtu_uart0_rx_callback, (osal_kthread_handler)dtu_uart0_task);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    if (dtu_storage_current_mode() != DTU_MODE_RUN) {
        return ERRCODE_SUCC;
    }

    ret = dtu_uart_init_context(&g_uart1_485_ctx, dtu_uart1_485_rx_callback,
        (osal_kthread_handler)dtu_uart1_485_task);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("UART1/485 init failed in RUN mode: 0x%x", ret);
    }
    return ret;
}

/**
 * @brief UART传输通道发送实现
 * @details 通过UART0发送数据，这是service transport表使用的主要接口
 *
 * @param[in] data 要发送的数据指针
 * @param[in] len 数据长度
 * @return ERRCODE_SUCC成功，其他失败
 *
 * @note 该函数只通过UART0发送，UART1/485使用专门的发送函数
 */
static errcode_t dtu_uart_transport_send_impl(const uint8_t *data, uint16_t len)
{
    return dtu_uart_send_context(&g_uart0_ctx, data, len);
}

/**
 * @brief 向PC发送数据
 * @details RUN bridge使用：UART0输出给PC观察
 *
 * @param[in] data 要发送的数据指针
 * @param[in] len 数据长度
 * @return ERRCODE_SUCC成功，其他失败
 */
errcode_t dtu_uart_send_to_pc(const uint8_t *data, uint16_t len)
{
    return dtu_uart_send_context(&g_uart0_ctx, data, len);
}

/**
 * @brief 向485总线发送数据
 * @details RUN bridge使用：UART1输出给485总线
 *
 * @param[in] data 要发送的数据指针
 * @param[in] len 数据长度
 * @return ERRCODE_SUCC成功，其他失败
 */
errcode_t dtu_uart_send_to_485(const uint8_t *data, uint16_t len)
{
    return dtu_uart_send_context(&g_uart1_485_ctx, data, len);
}

/**
 * @brief UART传输通道接口对象
 * @details 定义了UART传输通道的名称、初始化函数和发送函数
 */
const dtu_transport_if_t g_dtu_uart_transport = {
    .name = "UART0",
    .init = dtu_uart_transport_init_impl,
    .send = dtu_uart_transport_send_impl
};
