/**
 * @file FUYA.c
 * @brief 负压风机输出控制与墙面/地面工况切换
 * @details
 * 功能分三层：
 * 1) 百分比与 PWM 互转：统一调参接口与底层输出量纲；
 * 2) 工况识别：依据重力向量 Z 分量判断地面/墙面；
 * 3) 输出平滑：限制每周期步进，避免电源冲击和机械突变。
 *
 * 该模块默认在 10ms 周期内调用，参数阈值优先保证稳定性与可调性。
 */
#include "zf_common_headfile.h"
#include "FUYA.h"

/* --- 负压输出量纲定义 --- */
#define FUYA_PERCENT_MIN 0
#define FUYA_PERCENT_MAX 100
#define FUYA_PWM_MIN 1000
#define FUYA_PWM_MAX 2000

/* --- 表面识别阈值（基于重力向量 Z 分量） --- */
#define FUYA_GROUND_ENTER_VZ 0.86f
#define FUYA_WALL_ENTER_VZ 0.72f

/* --- 输出平滑步进（10ms 调用一次） --- */
#define FUYA_PWM_STEP_UP 40
#define FUYA_PWM_STEP_DOWN 30

volatile int fuya_date = FUYA_PWM_MIN;       /* 当前平滑后的负压输出脉宽 */
volatile int fuya_target_pwm = FUYA_PWM_MIN; /* 当前目标脉宽 */
volatile uint8 fuya_target_percent = 0;      /* 当前目标百分比 */
volatile uint8 fuya_surface_state = FUYA_SURFACE_GROUND;

static uint8 fuya_limit_percent(int percent)
{
    /* 上层写入统一裁剪为 0~100，避免后续映射越界 */
    if (percent < FUYA_PERCENT_MIN)
    {
        percent = FUYA_PERCENT_MIN;
    }
    else if (percent > FUYA_PERCENT_MAX)
    {
        percent = FUYA_PERCENT_MAX;
    }
    return (uint8)percent;
}

/**
 * @brief 负压百分比映射到 PWM 脉宽
 * @param percent 0~100 的目标负压百分比
 * @return 对应 PWM 脉宽，范围 FUYA_PWM_MIN~FUYA_PWM_MAX
 */
static int fuya_percent_to_pwm(uint8 percent)
{
    return (int)(FUYA_PWM_MIN + ((int)percent * (FUYA_PWM_MAX - FUYA_PWM_MIN)) / FUYA_PERCENT_MAX);
}

/**
 * @brief 根据重力向量 Z 分量识别地面/墙面
 * @details 使用双阈值切换，减少边界抖动引起的状态来回跳变。
 */
static uint8 fuya_detect_surface(float vzc)
{
    uint8 next_state;

    /* 滞回切换：地面转墙面阈值更低，墙面回地面阈值更高 */
    next_state = fuya_surface_state;
    if (FUYA_SURFACE_GROUND == fuya_surface_state)
    {
        if (vzc <= FUYA_WALL_ENTER_VZ)
        {
            next_state = FUYA_SURFACE_WALL;
        }
    }
    else
    {
        if (vzc >= FUYA_GROUND_ENTER_VZ)
        {
            next_state = FUYA_SURFACE_GROUND;
        }
    }

    return next_state;
}

/**
 * @brief PWM 输出斜坡器
 * @details 按上升/下降不同步进平滑过渡，降低负压系统突变。
 */
static int fuya_ramp_pwm(int current_pwm, int target_pwm)
{
    int delta;
    int step;

    /* 先求差值，后按方向选择上升/下降步进 */
    delta = target_pwm - current_pwm;
    if (0 == delta)
    {
        return current_pwm;
    }

    if (delta > 0)
    {
        /* 上升沿采用更快步进，提高贴墙阶段响应 */
        step = FUYA_PWM_STEP_UP;
        if (delta < step)
        {
            step = delta;
        }
        return current_pwm + step;
    }

    /* 下降沿稍慢，避免负压瞬降导致姿态抖动 */
    step = FUYA_PWM_STEP_DOWN;
    if (-delta < step)
    {
        step = -delta;
    }
    return current_pwm - step;
}

/**
 * @brief 负压模块初始化
 * @details 初始化 PWM 通道并复位内部状态到安全默认值。
 */
void fuya_Init(void)
{
    pwm_init(PWMA_CH2N_P03, 100, 0);

    /* 统一复用停机路径，确保初始化与保护停机行为一致 */
    fuya_force_stop();
}

/**
 * @brief 负压输出底层接口
 * @details 对 PWM 做最终限幅，避免越界写入。
 */
void fuya_motor_output(int pwm)
{
    if (pwm < FUYA_PWM_MIN)
    {
        pwm = FUYA_PWM_MIN;
    }
    else if (pwm > FUYA_PWM_MAX)
    {
        pwm = FUYA_PWM_MAX;
    }

    pwm_set_duty(PWMA_CH2N_P03, (uint32)pwm);
}

/**
 * @brief 直接设置负压目标百分比
 * @details 该接口用于菜单或调参命令快速写入，默认立即生效。
 */
void fuya_set_percent(uint8 percent)
{
    uint8 limited_percent;
    int pwm_value;

    /* 菜单/上位机统一按百分比输入，内部转换为 ESC 脉宽 */
    limited_percent = fuya_limit_percent((int)percent);
    pwm_value = fuya_percent_to_pwm(limited_percent);

    fuya_target_percent = limited_percent;
    fuya_target_pwm = pwm_value;
    fuya_date = pwm_value;
    fuya_motor_output(pwm_value);
}

/**
 * @brief 强制关闭负压
 * @details 用于停止态或保护态，确保风机输出回到最小占空。
 */
void fuya_force_stop(void)
{
    fuya_target_percent = 0;
    fuya_target_pwm = FUYA_PWM_MIN;
    fuya_date = FUYA_PWM_MIN;
    fuya_surface_state = FUYA_SURFACE_GROUND;
    fuya_motor_output(FUYA_PWM_MIN);
}

/**
 * @brief 简化负压更新流程
 * @details
 * 读取姿态 -> 识别工况 -> 选择目标负压 -> 斜坡输出。
 * 该流程适合 10ms 周期调度，兼顾响应与稳定。
 */
void fuya_update_simple(void)
{
    float vzc;
    uint8 next_state;
    uint8 target_percent;
    int target_pwm;
    int output_pwm;

    /* 1) 姿态输入：只取重力向量 Z 分量即可完成墙面识别 */
    imu_update_gravity_vector_from_quaternion(0, 0, &vzc);
    vzc = func_limit(vzc, 1.0f);

    /* 2) 识别当前表面工况 */
    next_state = fuya_detect_surface(vzc);
    fuya_surface_state = next_state;

    /* 3) 按工况选择目标百分比（墙面优先用 wall 参数） */
    if (FUYA_SURFACE_WALL == next_state)
    {
        target_percent = fuya_limit_percent((int)(app.start.fuya_wall_percent + 0.5f));
    }
    else
    {
        target_percent = fuya_limit_percent((int)(app.start.fuya_xili + 0.5f));
    }

    /* 4) 目标映射与斜坡输出，防止负压突跳 */
    target_pwm = fuya_percent_to_pwm(target_percent);
    output_pwm = fuya_ramp_pwm(fuya_date, target_pwm);

    fuya_target_percent = target_percent;
    fuya_target_pwm = target_pwm;
    fuya_date = output_pwm;

    fuya_motor_output(output_pwm);
}
