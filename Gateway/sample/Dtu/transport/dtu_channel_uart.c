/**
 * @file dtu_channel_uart.c
 * @brief UART0 配置/观察口和 UART1/485 业务口 transport 实现。
 */

#include "dtu_transport.h"

#include "dma.h"
#include "dtu_log.h"
#include "dtu_service.h"
#include "dtu_storage.h"
#include "gpio.h"
#include "hal_dma.h"
#include "hal_reboot.h"
#include "osal_debug.h"
#include "pinctrl.h"
#include "soc_osal.h"

/* 每路 UART 用独立 context 管理硬件、缓冲和任务资源。 */
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

/* UART0 始终启动，使用固定配置，避免配置口被错误 NV 参数锁死。 */
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

/* UART1/485 只在 RUN 模式初始化，参数来自 runtime 配置。 */
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

/* Ring buffer: RX callback 只入队，transport task 再批量提交给 manager。 */
static uint16_t dtu_uart_ring_used(const dtu_uart_ctx_t *ctx)
{
    if (ctx->rx_head >= ctx->rx_tail) {
        return (uint16_t)(ctx->rx_head - ctx->rx_tail);
    }
    return (uint16_t)(DTU_CFG_RING_BUFFER_SIZE - ctx->rx_tail + ctx->rx_head);
}

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

static bool dtu_uart_ring_pop(dtu_uart_ctx_t *ctx, uint8_t *byte)
{
    if (ctx->rx_tail == ctx->rx_head) {
        return false;
    }
    *byte = ctx->rx_ring[ctx->rx_tail];
    ctx->rx_tail = (uint16_t)((ctx->rx_tail + 1) % DTU_CFG_RING_BUFFER_SIZE);
    return true;
}

/* RX callback 只搬运数据，不做协议解析或 RUN 业务处理。 */
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
        if (!dtu_uart_ring_push(ctx, data[i])) { // ring 满后停止接纳本批剩余字节。
            break;
        }
        accepted++;
    }

    dtu_service_trace_rx_batch(length, accepted, dtu_uart_ring_used(ctx)); // 记录本批接收和 ring 水位。
    osal_sem_up(&ctx->rx_sem); // 唤醒对应 UART transport task 批量取数。
}

static void dtu_uart0_rx_callback(const void *buffer, uint16_t length, bool error)
{
    dtu_uart_rx_to_context(&g_uart0_ctx, buffer, length, error); // UART0 RX 只入 UART0 ring。
}

static void dtu_uart1_485_rx_callback(const void *buffer, uint16_t length, bool error)
{
    dtu_uart_rx_to_context(&g_uart1_485_ctx, buffer, length, error); // UART1/485 RX 只入 485 ring。
}

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

/* UART0 使用固定配置；UART1/485 使用 runtime 配置。 */
static const dtu_uart_cfg_t *dtu_uart_effective_cfg(const dtu_uart_ctx_t *ctx)
{
    if (ctx != NULL && ctx->use_runtime_cfg) {
        return &dtu_storage_runtime_const()->uart_cfg;
    }
    return (ctx == NULL) ? NULL : &ctx->fixed_cfg;
}

static void dtu_uart_prepare_tx_pin(const dtu_uart_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->bus != DTU_CFG_485_UART_BUS) {
        (void)uapi_pin_set_mode(ctx->tx_pin, ctx->pin_mode);
        return;
    }

    (void)uapi_pin_set_mode(ctx->tx_pin, PIN_MODE_0);
    (void)uapi_gpio_set_dir(ctx->tx_pin, GPIO_DIRECTION_OUTPUT);
    (void)uapi_gpio_set_val(ctx->tx_pin, GPIO_LEVEL_HIGH);
    (void)uapi_pin_set_mode(ctx->tx_pin, ctx->pin_mode);
}

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
    dtu_storage_fill_uart_attr(&uart_attr, effective_cfg); // 将 DTU 协议串口参数转成 SDK UART 参数。
    extra_attr.rx_int_threshold = dtu_storage_rx_int_threshold(); // 按 CONFIG/RUN profile 选择 RX 水位。

    uapi_uart_deinit(ctx->bus);
    dtu_uart_prepare_tx_pin(ctx); // UART1/485 先预置 TX 空闲高电平，再切回 UART 功能。
    uapi_pin_set_mode(ctx->rx_pin, ctx->pin_mode);

    ret = osal_sem_binary_sem_init(&ctx->rx_sem, 0); // callback 和 transport task 之间用信号量解耦。
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

    task = osal_kthread_create(task_handler, NULL, ctx->task_name, DTU_CFG_TRANSPORT_TASK_STACK_SIZE); // 每路 UART 独立任务批量处理 ring 数据。
    if (task == NULL) {
        return ERRCODE_FAIL;
    }
    osal_kthread_set_priority(task, DTU_CFG_TRANSPORT_TASK_PRIO);
    return ERRCODE_SUCC;
}

/* UART 发送统一走 DMA。 */
static errcode_t dtu_uart_send_context(dtu_uart_ctx_t *ctx, const uint8_t *data, uint16_t len)
{
    int32_t ret;

    if (ctx == NULL || data == NULL || len == 0) {
        return ERRCODE_FAIL;
    }
    ret = uapi_uart_write_by_dma(ctx->bus, data, len, &ctx->dma_cfg); // 发送路径统一走 DMA，减少 task 占用。
#if defined(CONFIG_UART_USING_V151)
    return (ret == ERRCODE_SUCC) ? ERRCODE_SUCC : (errcode_t)ret;
#else
    return (ret == (int32_t)len) ? ERRCODE_SUCC : (errcode_t)ret;
#endif
}

static void *dtu_uart0_task(const char *arg)
{
    uint8_t batch[DTU_CFG_TRANSPORT_RX_BATCH_SIZE];

    unused(arg);
    while (1) {
        uint16_t count = dtu_uart_pop_batch(&g_uart0_ctx, batch, sizeof(batch)); // 从 UART0 ring 批量取配置/调试字节。

        if (count > 0) {
            dtu_service_on_bytes(DTU_TRANSPORT_UART, batch, count); // 交给 manager 按 CONFIG/RUN 分流。
            continue;
        }

        if (dtu_storage_is_reboot_pending()) {
            osal_msleep(DTU_CFG_REBOOT_DELAY_MS); // 等待 REBOOT 回包离开串口。
            hal_reboot_chip(); // 在 UART0 任务安全点执行真正复位。
        }

        dtu_service_trace_rx_task_wakeup();
        if (osal_sem_down(&g_uart0_ctx.rx_sem) != OSAL_SUCCESS) {
            osal_msleep(DTU_CFG_TASK_IDLE_RETRY_MS);
        }
    }

    return NULL;
}

static void *dtu_uart1_485_task(const char *arg)
{
    uint8_t batch[DTU_CFG_TRANSPORT_RX_BATCH_SIZE];

    unused(arg);
    while (1) {
        uint16_t count = dtu_uart_pop_batch(&g_uart1_485_ctx, batch, sizeof(batch)); // 从 485 ring 批量取外设回包。

        if (count > 0) {
            dtu_service_on_uart485_bytes(batch, count); // 485 返回数据进入 RUN 上行桥接。
            continue;
        }

        dtu_service_trace_rx_task_wakeup();
        if (osal_sem_down(&g_uart1_485_ctx.rx_sem) != OSAL_SUCCESS) {
            osal_msleep(DTU_CFG_TASK_IDLE_RETRY_MS);
        }
    }

    return NULL;
}

static errcode_t dtu_uart_transport_init_impl(void)
{
    errcode_t ret;

    uapi_dma_init(); // UART 发送依赖 DMA 控制器。
    uapi_dma_open(); // 打开 DMA 后再初始化 UART 通道。

    ret = dtu_uart_init_context(&g_uart0_ctx, dtu_uart0_rx_callback, (osal_kthread_handler)dtu_uart0_task); // UART0 始终初始化。
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    if (dtu_storage_current_mode() != DTU_MODE_RUN) { // CONFIG 模式不启动 UART1/485 业务口。
        return ERRCODE_SUCC;
    }

    ret = dtu_uart_init_context(&g_uart1_485_ctx, dtu_uart1_485_rx_callback,
        (osal_kthread_handler)dtu_uart1_485_task);
    if (ret != ERRCODE_SUCC) {
        dtu_log_error("UART1/485 init failed in RUN mode: 0x%x", ret);
    }
    return ret;
}

static errcode_t dtu_uart_transport_send_impl(const uint8_t *data, uint16_t len)
{
    return dtu_uart_send_context(&g_uart0_ctx, data, len);
}

errcode_t dtu_uart_send_to_pc(const uint8_t *data, uint16_t len)
{
    return dtu_uart_send_context(&g_uart0_ctx, data, len);
}

errcode_t dtu_uart_send_to_485(const uint8_t *data, uint16_t len)
{
    return dtu_uart_send_context(&g_uart1_485_ctx, data, len);
}

const dtu_transport_if_t g_dtu_uart_transport = {
    .name = "UART0",
    .init = dtu_uart_transport_init_impl,
    .send = dtu_uart_transport_send_impl
};
