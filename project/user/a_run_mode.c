/**
 * @file a_run_mode.c
 * @brief 运行模式统一调配层
 * @details
 * 本模块保留启停、负压状态更新和菜单调试读取接口。5ms 主控链路中的飞坡与
 * 赛道元素更新直接调用对应模块，避免高频路径经过只转发的包装函数。
 */
#include "zf_common_headfile.h"

/* --- 启停状态机参数 --- */
#define START_DEBOUNCE_TIME 2    /* 启动按键消抖确认次数，单位为 10ms 调用周期；当前约 20ms。 */
#define START_DELAY_TICKS 100    /* 进入运行态前的确认时间，单位为 10ms 调用周期；当前约 1s。 */
#define START_LED_ON 0           /* P43 指示灯为低电平点亮。 */
#define START_LED_OFF 1

enum StartState
{
    START_STATE_0 = 0, // 停止/待机
    START_STATE_1 = 1, // 预启动
    START_STATE_2 = 2  // 允许运行
};

static enum StartState current_start_state = START_STATE_0; /**< 当前启动状态，由 `a_run_mode_update_start_state` 写入，外部只读。 */
static int press_debounce_cnt = 0;                          /**< 按下消抖计数，仅在 `a_run_mode_update_start_state`（10ms 上下文）中递增。 */
static int8 key_released = 1;                               /**< 按键释放锁存：1-已释放等待下一次按下，0-仍在按下期间，防止重复触发状态切换。 */
static int start_delay_ticks = 0;                            /**< 预启动到运行态的倒计时，期间对外仍保持 START_STATE_1。 */

/**
 * @brief 启动状态机更新
 * @details 10ms 调用一次，检测 P36 启动按键，在停止、预启动、运行之间切换。
 *          按键为低电平有效，持续按住只触发一次，释放后才允许下一次切换。
 *          第二次按键后先点亮 P43 约 1s，确认窗口结束后才真正进入运行态。
 */
void a_run_mode_update_start_state(void)
{
    if (start_delay_ticks > 0)
    {
        start_delay_ticks--;
        if (start_delay_ticks == 0)
        {
            P43 = START_LED_OFF;
            current_start_state = START_STATE_2;
        }
        return;
    }

    if (P36 != 0)
    {
        press_debounce_cnt = 0;
        key_released = 1;
        return;
    }

    if (key_released == 0)
    {
        return;
    }

    press_debounce_cnt++;
    if (press_debounce_cnt < START_DEBOUNCE_TIME)
    {
        return;
    }

    if (current_start_state == START_STATE_1)
    {
        start_delay_ticks = START_DELAY_TICKS;
        P43 = START_LED_ON;
    }
    else
    {
        current_start_state = START_STATE_1;
        P43 = START_LED_OFF;
    }
    /* 按住期间锁存触发结果，避免长按或触点抖动导致状态连续跳变。 */
    key_released = 0;
    press_debounce_cnt = 0;
}

/**
 * @brief 读取当前启动状态
 * @return int8 当前状态值：0-停止，1-预启动，2-运行中
 */
int8 a_run_mode_get_start_state(void)
{
    return (int8)current_start_state;
}

/**
 * @brief 负压状态更新
 * @details 启动状态有效且配置允许时才更新负压，避免待机时误动作
 */
void a_run_mode_update_fuya_state(void)
{
    if (current_start_state >= START_STATE_1 && app.start.start_flag == 1)
    {
        fuya_set_percent(app.start.fuya_xili);
    }
    else
    {
        fuya_stop();
    }
}

