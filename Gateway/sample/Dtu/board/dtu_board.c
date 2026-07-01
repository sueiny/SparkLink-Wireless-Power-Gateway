#include "dtu_board.h"

#include "dtu_storage.h"
#include "hal_reboot.h"
#include "osal_debug.h"
#include "osal_timer.h"

/* 板级 IO：
 * 1. GPIO13 DIP：高电平 CONFIG，低电平 RUN
 * 2. 状态灯：IO0 蓝、IO1 绿、IO2 红，CONFIG 红，RUN ROOT 绿，RUN NODE 蓝
 * 3. 活动灯：IO8 蓝、IO10 绿、IO9 红，数据流通时白色闪烁，空闲自动熄灭
 * 4. IO14 恢复出厂按键：默认高电平，按下保持低电平 1s 后写入默认配置并重启
 * 5. 两组三色灯均为共阳，低电平点亮
 * 6. 485 方向由 UART1_TX 经三极管硬件控制，软件不再驱动独立 DE//RE GPIO
 */

static bool g_activity_timer_ready = false;
static osal_timer g_activity_off_timer;
static bool g_factory_timer_ready = false;
static bool g_factory_reset_pending = false;
static osal_timer g_factory_reset_timer;

static gpio_level_t dtu_board_led_level(bool on)
{
    return on ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
}

static void dtu_board_led_pin_init(pin_t pin)
{
    (void)uapi_pin_set_mode(pin, DTU_CFG_LED_PIN_MODE);
    (void)uapi_pin_set_pull(pin, PIN_PULL_TYPE_DISABLE);
    (void)uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
    (void)uapi_gpio_set_val(pin, dtu_board_led_level(false));
}


static void dtu_board_set_state_led(bool blue, bool green, bool red)
{
    (void)uapi_gpio_set_val(DTU_CFG_STATE_LED_BLUE_PIN, dtu_board_led_level(blue));
    (void)uapi_gpio_set_val(DTU_CFG_STATE_LED_GREEN_PIN, dtu_board_led_level(green));
    (void)uapi_gpio_set_val(DTU_CFG_STATE_LED_RED_PIN, dtu_board_led_level(red));
}

static void dtu_board_set_activity_led(bool blue, bool green, bool red)
{
    (void)uapi_gpio_set_val(DTU_CFG_ACTIVITY_LED_BLUE_PIN, dtu_board_led_level(blue));
    (void)uapi_gpio_set_val(DTU_CFG_ACTIVITY_LED_GREEN_PIN, dtu_board_led_level(green));
    (void)uapi_gpio_set_val(DTU_CFG_ACTIVITY_LED_RED_PIN, dtu_board_led_level(red));
}

static void dtu_board_activity_timer_cb(unsigned long data)
{
    unused(data);
    dtu_board_set_activity_led(false, false, false); // 活动灯定时到期后自动熄灭。
}

static void dtu_board_factory_enable_irq(void)
{
    (void)uapi_gpio_clear_interrupt(DTU_CFG_FACTORY_KEY_PIN);
    (void)uapi_gpio_enable_interrupt(DTU_CFG_FACTORY_KEY_PIN);
}

static void dtu_board_factory_timer_cb(unsigned long data)
{
    errcode_t ret;

    unused(data);

    if (uapi_gpio_get_val(DTU_CFG_FACTORY_KEY_PIN) != GPIO_LEVEL_LOW) {
        g_factory_reset_pending = false;
        dtu_board_factory_enable_irq(); // 松手未满 3s 时恢复监听。
        return;
    }

    osal_printk("[DTU] factory key confirmed, factory reset and reboot\r\n");
    ret = dtu_storage_factory_reset(); // 复用协议恢复出厂路径，统一默认配置语义。
    if (ret != ERRCODE_SUCC) {
        osal_printk("[DTU] factory key reset failed ret=0x%X\r\n", ret);
    }
    hal_reboot_chip();
}

static void dtu_board_factory_key_isr(pin_t pin, uintptr_t param)
{
    unused(pin);
    unused(param);

    if (!g_factory_timer_ready || g_factory_reset_pending) {
        return;
    }

    g_factory_reset_pending = true;
    (void)uapi_gpio_disable_interrupt(DTU_CFG_FACTORY_KEY_PIN);
    (void)uapi_gpio_clear_interrupt(DTU_CFG_FACTORY_KEY_PIN);
    (void)osal_timer_start(&g_factory_reset_timer); // 中断里只启动确认定时器。
}

static void dtu_board_factory_key_init(void)
{
    errcode_t ret;

    (void)uapi_pin_set_mode(DTU_CFG_FACTORY_KEY_PIN, DTU_CFG_FACTORY_KEY_PIN_MODE);
    (void)uapi_pin_set_pull(DTU_CFG_FACTORY_KEY_PIN, DTU_CFG_FACTORY_KEY_PULL);
    (void)uapi_gpio_set_dir(DTU_CFG_FACTORY_KEY_PIN, GPIO_DIRECTION_INPUT);

    g_factory_reset_timer.timer = NULL;
    g_factory_reset_timer.handler = dtu_board_factory_timer_cb;
    g_factory_reset_timer.data = 0;
    g_factory_reset_timer.interval = DTU_CFG_FACTORY_KEY_HOLD_MS;
    g_factory_timer_ready = (osal_timer_init(&g_factory_reset_timer) == 0);
    if (!g_factory_timer_ready) {
        osal_printk("[DTU] factory key timer init failed\r\n");
        return;
    }

    ret = uapi_gpio_register_isr_func(DTU_CFG_FACTORY_KEY_PIN, GPIO_INTERRUPT_FALLING_EDGE,
        dtu_board_factory_key_isr);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[DTU] factory key isr register failed ret=0x%X\r\n", ret);
        return;
    }
    dtu_board_factory_enable_irq(); // IO14 默认高电平，下降沿进入 1s 确认。
}

dtu_mode_t dtu_board_detect_mode(void)
{
    (void)uapi_pin_set_mode(DTU_CFG_MODE_SWITCH_PIN, DTU_CFG_MODE_SWITCH_PIN_MODE);
    (void)uapi_pin_set_pull(DTU_CFG_MODE_SWITCH_PIN, DTU_CFG_MODE_SWITCH_PIN_PULL);
    (void)uapi_gpio_set_dir(DTU_CFG_MODE_SWITCH_PIN, GPIO_DIRECTION_INPUT);

    return (uapi_gpio_get_val(DTU_CFG_MODE_SWITCH_PIN) == GPIO_LEVEL_HIGH) ?  DTU_MODE_RUN : DTU_MODE_CONFIG; // 拨码高电平进 CONFIG。
}

void dtu_board_mark_data_activity(void)
{
    dtu_board_set_activity_led(true, true, true); // 有数据流动时活动灯白色闪烁。
    if (g_activity_timer_ready) {
        (void)osal_timer_stop(&g_activity_off_timer);
        (void)osal_timer_start(&g_activity_off_timer); // 重新计时，保持短暂活动提示。
    }
}

errcode_t dtu_board_init(void)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();
    (void)uapi_gpio_init();
    (void)dtu_board_detect_mode(); // 初始化阶段确保 DIP 引脚按输入模式配置。

    dtu_board_led_pin_init(DTU_CFG_STATE_LED_BLUE_PIN);
    dtu_board_led_pin_init(DTU_CFG_STATE_LED_GREEN_PIN);
    dtu_board_led_pin_init(DTU_CFG_STATE_LED_RED_PIN);
    dtu_board_led_pin_init(DTU_CFG_ACTIVITY_LED_BLUE_PIN);
    dtu_board_led_pin_init(DTU_CFG_ACTIVITY_LED_GREEN_PIN);
    dtu_board_led_pin_init(DTU_CFG_ACTIVITY_LED_RED_PIN);

    if (dtu_storage_current_mode() == DTU_MODE_CONFIG) {
        dtu_board_set_state_led(false, false, true); // CONFIG 模式亮红灯。
    } else {
        dtu_board_set_state_led(cfg->role != DTU_CFG_ROLE_ROOT, cfg->role == DTU_CFG_ROLE_ROOT, false); // RUN 模式按 ROOT/NODE 显示绿/蓝。
    }
    dtu_board_set_activity_led(false, false, false);

    g_activity_off_timer.timer = NULL;
    g_activity_off_timer.handler = dtu_board_activity_timer_cb;
    g_activity_off_timer.data = 0;
    g_activity_off_timer.interval = DTU_CFG_ACTIVITY_LED_HOLD_MS;
    g_activity_timer_ready = (osal_timer_init(&g_activity_off_timer) == 0); // timer 可用时活动灯支持自动熄灭。
    dtu_board_factory_key_init(); // IO14 长按 1s 后复用恢复出厂路径并重启。
    return ERRCODE_SUCC;
}
