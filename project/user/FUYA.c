/**
 * @file FUYA.c
 * @brief 负压风机固定输出控制
 * @details
 * 功能分三层：
 * 1) 百分比与 PWM 互转：统一调参接口与底层输出量纲（50Hz 下 500~1000 对应 0~100%）；
 * 2) 预启动固定输出：由上层启停状态机按 `app.start.fuya_xili` 直接给定；
 * 3) 圆桶标志保留：仅保留圆桶过顶标志位，不再动态切换负压或循迹参数。
 *
 * 资源与时序：本模块初始化后保持停机，兼容旧周期入口但不改变输出。
 */
#include "zf_common_headfile.h"
#include "FUYA.h"
// 计算无刷电调转速   （1ms - 2ms）/20ms * 10000（10000是PWM的满占空比时候的值）
// 在50Hz的控制频率下，无刷电调转速 0%   为 500
// 在50Hz的控制频率下，无刷电调转速 20%  为 600
// 在50Hz的控制频率下，无刷电调转速 40%  为 700
// 在50Hz的控制频率下，无刷电调转速 60%  为 800
// 在50Hz的控制频率下，无刷电调转速 80%  为 900
// 在50Hz的控制频率下，无刷电调转速 100% 为 1000

// 电调支持50hz-300hz的控制频率
// 50Hz的控制频率 ，从0%到100%占空比为500到1000
// 100Hz的控制频率，从0%到100%占空比为1000到2000
// 200Hz的控制频率，从0%到100%占空比为2000到4000
// 300Hz的控制频率，从0%到100%占空比为3000到6000
/* --- 负压输出量纲定义 --- */
#define FUYA_PERCENT_MIN 0
#define FUYA_PERCENT_MAX 100
#define FUYA_PWM_MIN 500
#define FUYA_PWM_MAX 1000

/* --- 圆筒最高点识别阈值（10ms 调用一次） --- */
#define FUYA_CYLINDER_GROUND_ROLL_ABS_DEG 26.0f
#define FUYA_CYLINDER_TOP_ROLL_ABS_DEG 150.0f
#define FUYA_CYLINDER_TOP_CONFIRM_COUNT 2
#define FUYA_CYLINDER_GROUND_CONFIRM_COUNT 3

volatile int fuya_date = FUYA_PWM_MIN;       /* 当前固定负压输出脉宽 */
volatile int fuya_target_pwm = FUYA_PWM_MIN; /* 当前目标脉宽 */
volatile uint8 fuya_target_percent = 0;      /* 当前目标百分比 */
volatile uint8 fuya_cylinder_peak_flag = 0; /* 圆筒最高点通过标志 */
volatile float fuya_last_vzc = 0.0f;        /* 最近一次 roll 角差，单位：度，范围 -180~180。 */

static uint8 fuya_cylinder_top_count = 0;    /* 顶部大角度窗口连续计数 */
static uint8 fuya_cylinder_ground_count = 0; /* 回平地连续计数 */
static float fuya_read_vzc(void);

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
 * @brief 读取当前 roll 角差
 * @details 直接使用 5ms IMU 缓存值，避免负压和圆桶各自重复读取姿态角。
 */
static float fuya_read_vzc(void)
{
    float vzc;

    vzc = imu_get_gravity_vz();
    fuya_last_vzc = vzc;
    return vzc;
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
 * @brief 进入圆筒最高点处理窗口
 * @details
 * 当前已取消圆桶过程对 A_1/B_1/C_l 的临时切换。
 * 该接口仅保留圆桶流程标志位，避免旧调用点重新启用时再改写循迹参数。
 */
void fuya_apply_cylinder_peak_angle(void)
{
    fuya_cylinder_peak_flag = 1;
}

/**
 * @brief 恢复圆筒最高点触发前的 angle 参数
 * @details
 * 当前圆桶过程不再切换 A_1/B_1/C_l，因此这里仅清理圆桶相关计数和标志。
 */
void fuya_restore_cylinder_peak_angle(void)
{
    fuya_cylinder_top_count = 0;
    fuya_cylinder_ground_count = 0;
    fuya_cylinder_peak_flag = 0;
}

/**
 * @brief 负压模块初始化
 * @details 初始化 PWM 通道并保持 ESC 最小脉宽，等待启停状态机进入预启动后再拉负压。
 */
void fuya_Init(void)
{
    pwm_init(PWMA_CH2N_P03, 50, 0);

    /*
     * 上电不能直接拉负压，避免还在菜单/调参阶段时风机误启动。
     * 固定输出由 run_time_2() 中的预启动状态统一触发。
     */
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
 * @details 该接口用于预启动固定值、菜单或调参命令写入；写入后立即输出，不再按姿态动态更新。
 */
void fuya_set_percent(uint8 percent)
{
    uint8 limited_percent;
    int pwm_value;

    /* 固定负压不再依赖周期调用，必须一次写到目标 PWM。 */
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
    fuya_restore_cylinder_peak_angle();
    fuya_motor_output(FUYA_PWM_MIN);
}

/**
 * @brief 兼容旧调用点的固定负压刷新入口
 * @details
 * 当前负压只允许由 run_time_2() 中的预启动状态显式拉起。
 * 旧的周期调用保留为空入口，避免历史路径绕过启停状态机直接启动风机。
 */
void fuya_update_simple(void)
{
}

/**
 * @brief 10ms 圆筒最高点检测
 * @details
 * 判定逻辑为：检测 roll 角差在圆筒顶部连续进入大角度区间，
 * 以此确认已到达圆筒最高点附近。回到平地后清除圆桶过顶标志。
 */
void fuya_update_cylinder_peak_10ms(int8 start_state)
{
    float roll_abs_deg;
    float vzc;

    if (2 != start_state)
    {
        fuya_restore_cylinder_peak_angle();
        return;
    }

    vzc = fuya_read_vzc();
    roll_abs_deg = vzc;
    if (roll_abs_deg < 0.0f)
    {
        roll_abs_deg = -roll_abs_deg;
    }

    if (!fuya_cylinder_peak_flag)
    {
        if (roll_abs_deg >= FUYA_CYLINDER_TOP_ROLL_ABS_DEG)
        {
            fuya_cylinder_top_count++;

            if (fuya_cylinder_top_count >= FUYA_CYLINDER_TOP_CONFIRM_COUNT)
            {
                fuya_apply_cylinder_peak_angle();
            }
        }
        else
        {
            fuya_cylinder_top_count = 0;
        }

        return;
    }

    if (roll_abs_deg <= FUYA_CYLINDER_GROUND_ROLL_ABS_DEG)
    {
        fuya_cylinder_ground_count++;

        if (fuya_cylinder_ground_count >= FUYA_CYLINDER_GROUND_CONFIRM_COUNT)
        {
            fuya_restore_cylinder_peak_angle();
        }
    }
    else
    {
        fuya_cylinder_ground_count = 0;
    }
}
