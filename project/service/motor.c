#include "motor.h"

#define MOTOR_START_PWM_RAMP_INITIAL_LIMIT 3000 /* 起步首个输出周期的 PWM 上限，单位：占空比。 */
#define MOTOR_START_PWM_RAMP_STEP 48            /* 2ms 非零输出加 48，约 250ms 从 3000 放开到 9000。 */
#define MOTOR_STALL_PWM_THRESHOLD 6000          /* 堵转判定的实际输出 PWM 下限，低于该值时不认为电机已强驱。 */
#define MOTOR_STALL_SPEED_THRESHOLD 2.0f        /* 堵转判定的编码器速度上限，单位同 PID.left_speed.speed。 */
#define MOTOR_STALL_CONFIRM_COUNT 80            /* 10ms 检测周期计数，80 次约 0.8s，用于过滤起步和瞬时卡顿。 */

/* 全局控制标志位 */
volatile uint8 stop = 0;   /* 停车标志位，1 表示紧急停车保护 */
volatile float dianya = 0; /* 当前电池电压值 */

static int32 motor_start_pwm_ramp_limit = MOTOR_START_PWM_RAMP_INITIAL_LIMIT;
static int32 motor_last_lpwm_limited = 0;
static int32 motor_last_rpwm_limited = 0;
static int16 motor_left_stall_count = 0;
static int16 motor_right_stall_count = 0;

/**
 * @brief 电机及相关硬件初始化
 * @details 配置 PWM 频率、分辨率及引脚方向
 */
void motor_Init(void)
{
    /*
     * 左电机 PWM 初始化
     * 使用 PWMB 通道 2，引脚 P13，频率 24kHz（避开人耳听觉频率，减小啸叫）
     */
    pwm_init(PWMB_CH2_P13, 17000, 0);
    /* 左电机方向控制引脚 P14，设置为推挽输出 */
    gpio_init(IO_P14, GPO, 0, GPO_PUSH_PULL);
    /*
     * 右电机 PWM 初始化
     * 使用 PWMB 通道 3，引脚 P52，频率 24kHz
     */
    pwm_init(PWMB_CH3_P52, 17000, 0);
    /* 右电机方向控制引脚 P53，设置为推挽输出 */
    gpio_init(IO_P53, GPO, 1, GPO_PUSH_PULL);
}

/**
 * @brief 按指定上限等比例限制左右 PWM 输出。
 * @param lpwm 已经过全局限幅的左轮目标 PWM 指针。
 * @param rpwm 已经过全局限幅的右轮目标 PWM 指针。
 * @param limit 本次允许的最大 PWM 绝对值，单位：占空比。
 *
 * 不能把左右轮分别截到同一个上限，否则会抹掉差速转向量；
 * 这里按较大一侧缩放两轮输出，在限制冲击的同时保留转向比例。
 */
static void motor_limit_pwm_pair_to(int32 *lpwm, int32 *rpwm, int32 limit)
{
    int32 left_abs;
    int32 right_abs;
    int32 max_abs;

    if (limit <= 0)
    {
        return;
    }

    left_abs = func_abs(*lpwm);
    right_abs = func_abs(*rpwm);
    max_abs = (left_abs > right_abs) ? left_abs : right_abs;

    if (max_abs <= limit)
    {
        return;
    }

    *lpwm = (*lpwm * limit) / max_abs;
    *rpwm = (*rpwm * limit) / max_abs;
}

/**
 * @brief 按起步爬坡窗口等比例限制左右 PWM 输出。
 * @param lpwm 已经过全局限幅的左轮目标 PWM 指针。
 * @param rpwm 已经过全局限幅的右轮目标 PWM 指针。
 */
static void motor_limit_start_pwm_pair(int32 *lpwm, int32 *rpwm)
{
    motor_limit_pwm_pair_to(lpwm, rpwm, motor_start_pwm_ramp_limit);
}

/**
 * @brief 根据本周期输出推进或重置起步 PWM 爬坡窗口。
 * @param lpwm_limited 已经完成起步限幅后的左轮 PWM。
 * @param rpwm_limited 已经完成起步限幅后的右轮 PWM。
 *
 * 非运行态由上层传入 0 输出，本函数借此重置窗口；只有非零输出才消耗爬坡次数。
 */
static void motor_update_start_pwm_ramp(int32 lpwm_limited, int32 rpwm_limited)
{
    if (lpwm_limited == 0 && rpwm_limited == 0)
    {
        motor_start_pwm_ramp_limit = MOTOR_START_PWM_RAMP_INITIAL_LIMIT;
        motor_last_lpwm_limited = 0;
        motor_last_rpwm_limited = 0;
        motor_left_stall_count = 0;
        motor_right_stall_count = 0;
        return;
    }

    if (motor_start_pwm_ramp_limit < MOTOR_OUTPUT_PWM_LIMIT)
    {
        motor_start_pwm_ramp_limit += MOTOR_START_PWM_RAMP_STEP;
        if (motor_start_pwm_ramp_limit > MOTOR_OUTPUT_PWM_LIMIT)
        {
            motor_start_pwm_ramp_limit = MOTOR_OUTPUT_PWM_LIMIT;
        }
    }
}

/**
 * @brief 10ms 周期检测电机堵转并触发停车保护。
 *
 * 由 10ms 状态环调用。堵转只在起步爬坡已经放开到较高 PWM 后检测，
 * 避免刚起步时编码器速度尚未建立导致误判。
 */
void motor_stall_check_10ms(void)
{
    if (motor_start_pwm_ramp_limit < MOTOR_STALL_PWM_THRESHOLD)
    {
        motor_left_stall_count = 0;
        motor_right_stall_count = 0;
        return;
    }

    if ((motor_last_lpwm_limited > MOTOR_STALL_PWM_THRESHOLD || motor_last_lpwm_limited < -MOTOR_STALL_PWM_THRESHOLD) &&
        PID.left_speed.speed < MOTOR_STALL_SPEED_THRESHOLD)
    {
        motor_left_stall_count++;
    }
    else
    {
        motor_left_stall_count = 0;
    }

    if ((motor_last_rpwm_limited > MOTOR_STALL_PWM_THRESHOLD || motor_last_rpwm_limited < -MOTOR_STALL_PWM_THRESHOLD) &&
        PID.right_speed.speed < MOTOR_STALL_SPEED_THRESHOLD)
    {
        motor_right_stall_count++;
    }
    else
    {
        motor_right_stall_count = 0;
    }

    if (motor_left_stall_count >= MOTOR_STALL_CONFIRM_COUNT ||
        motor_right_stall_count >= MOTOR_STALL_CONFIRM_COUNT)
    {
        stop = 1;
        motor_left_stall_count = 0;
        motor_right_stall_count = 0;
    }
}

/**
 * @brief 电机 PWM 占空比输出控制
 * @details 根据 lpwm 和 rpwm 的正负号控制电机正反转
 * @param lpwm 左轮目标占空比（函数内会限幅到 ±MOTOR_OUTPUT_PWM_LIMIT）
 * @param rpwm 右轮目标占空比（函数内会限幅到 ±MOTOR_OUTPUT_PWM_LIMIT）
 */
void motor_output(int32 lpwm, int32 rpwm)
{
    int32 lpwm_limited;
    int32 rpwm_limited;

    lpwm_limited = func_limit(lpwm, MOTOR_OUTPUT_PWM_LIMIT);
    rpwm_limited = func_limit(rpwm, MOTOR_OUTPUT_PWM_LIMIT);

    /* 检查停车标志位，stop 为 0 时正常运行 */
    if (stop == 0)
    {
        motor_limit_start_pwm_pair(&lpwm_limited, &rpwm_limited);
        if (fly_pwm_output_limit > 0)
        {
            motor_limit_pwm_pair_to(&lpwm_limited, &rpwm_limited, fly_pwm_output_limit);
        }
        motor_update_start_pwm_ramp(lpwm_limited, rpwm_limited);
        motor_last_lpwm_limited = lpwm_limited;
        motor_last_rpwm_limited = rpwm_limited;

        /* --- 左电机控制逻辑（lpwm_limited → P13/P14） --- */
        if (lpwm_limited > 0)
        {
            P14 = 1;                                  /* 设置方向：正转 */
            pwm_set_duty(PWMB_CH2_P13, lpwm_limited); /* 设置 PWM 占空比 */
        }
        else if (lpwm_limited < 0)
        {
            P14 = 0;                                   /* 设置方向：反转 */
            pwm_set_duty(PWMB_CH2_P13, -lpwm_limited); /* 取绝对值输出 PWM */
        }
        else
        {
            pwm_set_duty(PWMB_CH2_P13, 0); /* 停止输出 */
        }

        /* --- 右电机控制逻辑（rpwm_limited → P52/P53） --- */
        if (rpwm_limited > 0)
        {
            P53 = 0; /* 设置方向：正转 */
            pwm_set_duty(PWMB_CH3_P52, rpwm_limited);
        }
        else if (rpwm_limited < 0)
        {
            P53 = 1; /* 设置方向：反转 */
            pwm_set_duty(PWMB_CH3_P52, -rpwm_limited);
        }
        else
        {
            pwm_set_duty(PWMB_CH3_P52, 0); /* 停止输出 */
        }
    }
    else
    {
        /*
         * 保护状态：强制输出极低占空比或直接设为 100
         * 此处 设置成 100，设置成0会出现电机无法完全停止的情况，可能是由于 PWM 输出的非线性或电机特性导致的死区现象
         * 100 的占空比足以让电机保持静止状态，同时也能在某些情况下提供微小的反向力矩来抵消外部扰动，从而更有效地实现停车保护
         */
        pwm_set_duty(PWMB_CH2_P13, 100);
        pwm_set_duty(PWMB_CH3_P52, 100);
    }
}

/**
 * @brief 丢线保护逻辑
 * @details 当四路电感连续低于阈值且飞坡未临时屏蔽保护时判定为丢线。
 */
void lost_lines(void)
{
    static int8 count = 0; /* 丢线确认计数器 */

    /*
     * 跷跷板 HOLD/RECOVER 前段会主动屏蔽丢线；若 RECOVER 超过 1s 仍未恢复，
     * 飞坡状态机会释放该屏蔽，让真实丢线重新触发停车保护。
     */
    if (ad1 < 3 && ad2 < 3 && ad3 < 3 && ad4 < 3 && fly_lost_line_blocked == 0)
    {
        count++;
    }
    else
    {
        count = 0; /* 检测到线，计数器清零 */
    }

    /* 连续 5 次检测到丢线，触发保护停车 */
    if (count > 5)
    {
        count = 0;
        stop = 1;
    }
}

/**
 * @brief 电池电压监测与欠压保护
 * @details 通过 ADC 采样 P05 引脚的分压电压来判断电池剩余电量
 */
void dianya_jiance(void)
{
    uint16 adc_raw;

    /* 执行 ADC 转换 */
    adc_raw = adc_convert(ADC_CH13_P05);
    /* 转换公式：ADC值 * 转换系数（0.0092 需要根据分压电路电阻比例计算） */
    dianya = (float)adc_raw * 0.0092f;

    /* 锂电池欠压判定已移除，当前仅保留电压采样供调试显示。 */
}
