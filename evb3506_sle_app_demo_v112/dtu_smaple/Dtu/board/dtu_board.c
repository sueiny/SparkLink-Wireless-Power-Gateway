#include "dtu_board.h"

#include "dtu_storage.h"
#include "osal_debug.h"
#include "osal_timer.h"

/* 板级 IO：
 * 1. GPIO13 DIP：高电平 CONFIG，低电平 RUN
 * 2. 状态灯：IO0 蓝、IO1 绿、IO2 红，CONFIG 红，RUN ROOT 绿，RUN NODE 蓝
 * 3. 活动灯：IO7 蓝、IO8 绿、IO9 红，数据流通时白色闪烁，空闲自动熄灭
 * 4. 两组三色灯均为共阳，低电平点亮
 */

static bool g_activity_timer_ready = false;
static osal_timer g_activity_off_timer;

static gpio_level_t dtu_board_led_level(bool on)
{
    return on ? GPIO_LEVEL_LOW : GPIO_LEVEL_HIGH;
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
    dtu_board_set_activity_led(false, false, false);
}

dtu_mode_t dtu_board_detect_mode(void)
{
    (void)uapi_pin_set_mode(DTU_CFG_MODE_SWITCH_PIN, DTU_CFG_MODE_SWITCH_PIN_MODE);
    (void)uapi_pin_set_pull(DTU_CFG_MODE_SWITCH_PIN, DTU_CFG_MODE_SWITCH_PIN_PULL);
    (void)uapi_gpio_set_dir(DTU_CFG_MODE_SWITCH_PIN, GPIO_DIRECTION_INPUT);

    return (uapi_gpio_get_val(DTU_CFG_MODE_SWITCH_PIN) == GPIO_LEVEL_HIGH) ?  DTU_MODE_RUN : DTU_MODE_CONFIG;
}

void dtu_board_mark_data_activity(void)
{
    dtu_board_set_activity_led(true, true, true);
    if (g_activity_timer_ready) {
        (void)osal_timer_stop(&g_activity_off_timer);
        (void)osal_timer_start(&g_activity_off_timer);
    }
}

errcode_t dtu_board_init(void)
{
    const dtu_runtime_cfg_t *cfg = dtu_storage_runtime_const();

    (void)uapi_gpio_init();
    (void)dtu_board_detect_mode();

    dtu_board_led_pin_init(DTU_CFG_STATE_LED_BLUE_PIN);
    dtu_board_led_pin_init(DTU_CFG_STATE_LED_GREEN_PIN);
    dtu_board_led_pin_init(DTU_CFG_STATE_LED_RED_PIN);
    dtu_board_led_pin_init(DTU_CFG_ACTIVITY_LED_BLUE_PIN);
    dtu_board_led_pin_init(DTU_CFG_ACTIVITY_LED_GREEN_PIN);
    dtu_board_led_pin_init(DTU_CFG_ACTIVITY_LED_RED_PIN);

    if (dtu_storage_current_mode() == DTU_MODE_CONFIG) {
        dtu_board_set_state_led(false, false, true);
    } else {
        dtu_board_set_state_led(cfg->role != DTU_CFG_ROLE_ROOT, cfg->role == DTU_CFG_ROLE_ROOT, false);
    }
    dtu_board_set_activity_led(false, false, false);

    g_activity_off_timer.timer = NULL;
    g_activity_off_timer.handler = dtu_board_activity_timer_cb;
    g_activity_off_timer.data = 0;
    g_activity_off_timer.interval = DTU_CFG_ACTIVITY_LED_HOLD_MS;
    g_activity_timer_ready = (osal_timer_init(&g_activity_off_timer) == 0);
    return ERRCODE_SUCC;
}
