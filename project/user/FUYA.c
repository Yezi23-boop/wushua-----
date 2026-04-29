/**
 * @file FUYA.c
 * @brief 负压风机输出控制与墙面/地面工况切换
 * @details
 * 功能分三层：
 * 1) 百分比与 PWM 互转：统一调参接口与底层输出量纲（50Hz 下 500~1000 对应 0~100%）；
 * 2) 工况识别：依据重力向量 Z 分量识别地面/墙面/立体桶过顶；
 * 3) 输出平滑：斜坡逼近防止电池瞬态大电流冲击跌落。
 *
 * 资源与时序：本模块主要在 10ms 周期（run_time_2）中调用，内部无任何死延时阻塞。
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

/* --- 表面识别阈值（基于重力向量 Z 分量） --- */
#define FUYA_GROUND_ENTER_VZ 0.86f
#define FUYA_WALL_ENTER_VZ 0.72f

/* --- 圆筒最高点识别阈值（10ms 调用一次） --- */
#define FUYA_CYLINDER_VZ 0.90f
#define FUYA_CYLINDER_TOP_VZ -0.85f
#define FUYA_CYLINDER_TOP_CONFIRM_COUNT 2
#define FUYA_CYLINDER_GROUND_CONFIRM_COUNT 3

/* --- 输出平滑步进（10ms 调用一次） --- */
/**< 负压首次拉起的 PWM 单次变化步进量，限制无刷电调启动浪涌 */
#define FUYA_STARTUP_PWM_STEP_UP 10
/**< 负压上拉的 PWM 单次变化步进量（逼近更快） */
#define FUYA_PWM_STEP_UP 40
/**< 负压下降的 PWM 单次变化步进量（逼近稍慢以防掉落） */
#define FUYA_PWM_STEP_DOWN 30

volatile int fuya_date = FUYA_PWM_MIN;       /* 当前平滑后的负压输出脉宽 */
volatile int fuya_target_pwm = FUYA_PWM_MIN; /* 当前目标脉宽 */
volatile uint8 fuya_target_percent = 0;      /* 当前目标百分比 */
volatile uint8 fuya_surface_state = FUYA_SURFACE_GROUND;
volatile uint8 fuya_cylinder_peak_flag = 0; /* 圆筒最高点通过标志 */
volatile float fuya_last_vzc = 0.0f;        /* 最近一次限幅后的重力向量 Z 分量 */

static uint8 fuya_cylinder_top_count = 0;    /* 顶部负值窗口连续计数 */
static uint8 fuya_cylinder_ground_count = 0; /* 回平地连续计数 */
static uint8 fuya_startup_ramp_active = 1;   /* 首次给负压百分比时启用小步进软启动 */
static float fuya_angle_backup_a1 = 0.0f;    /* 过顶前 A_1 备份 */
static float fuya_angle_backup_b1 = 0.0f;    /* 过顶前 B_1 备份 */
static float fuya_angle_backup_cl = 0.0f;    /* 过顶前 C_l 备份 */

static float fuya_read_vzc(void);
static int fuya_ramp_pwm_with_step(int current_pwm, int target_pwm, int up_step);
static int fuya_ramp_pwm_startup(int current_pwm, int target_pwm);

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
 * @brief 读取并限幅当前重力向量 Z 分量
 * @details 将 IMU 取值与边界裁剪集中到一个函数，减少重复代码。
 */
static float fuya_read_vzc(void)
{
    float vzc;

    imu_update_gravity_vector_from_quaternion(0, 0, &vzc);
    if (vzc > 1.0f)
    {
        vzc = 1.0f;
    }
    else if (vzc < -1.0f)
    {
        vzc = -1.0f;
    }

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
static int fuya_ramp_pwm_with_step(int current_pwm, int target_pwm, int up_step)
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
        step = up_step;
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

static int fuya_ramp_pwm(int current_pwm, int target_pwm)
{
    /* 运行中上升沿采用更快步进，提高贴墙阶段响应 */
    return fuya_ramp_pwm_with_step(current_pwm, target_pwm, FUYA_PWM_STEP_UP);
}

/**
 * @brief 负压启动软爬升
 * @details 停机后首次给百分比时采用更小步进，避免目标百分比过大造成电流浪涌。
 */
static int fuya_ramp_pwm_startup(int current_pwm, int target_pwm)
{
    int output_pwm;

    if (target_pwm <= FUYA_PWM_MIN)
    {
        fuya_startup_ramp_active = 1;
        return FUYA_PWM_MIN;
    }

    if (current_pwm <= FUYA_PWM_MIN && 0 == fuya_target_percent)
    {
        fuya_startup_ramp_active = 1;
    }

    if (fuya_startup_ramp_active)
    {
        output_pwm = fuya_ramp_pwm_with_step(current_pwm, target_pwm, FUYA_STARTUP_PWM_STEP_UP);
        if (output_pwm >= target_pwm)
        {
            fuya_startup_ramp_active = 0;
        }
        return output_pwm;
    }

    return fuya_ramp_pwm(current_pwm, target_pwm);
}

/**
 * @brief 进入圆筒最高点处理窗口
 * @details 首次触发时备份 angle 参数，并覆盖为过顶专用权重。
 */
void fuya_apply_cylinder_peak_angle(void)
{
    if (!fuya_cylinder_peak_flag)
    {
        fuya_angle_backup_a1 = app.angle.A_1;
        fuya_angle_backup_b1 = app.angle.B_1;
        fuya_angle_backup_cl = app.angle.C_l;
    }

    app.angle.A_1 = 1.40f;
    app.angle.B_1 = 0.00f;
    app.angle.C_l = 0.00f;

    fuya_cylinder_peak_flag = 1;
}

/**
 * @brief 恢复圆筒最高点触发前的 angle 参数
 * @details 退出运行态或回到平地时统一调用，避免临时参数残留。
 */
void fuya_restore_cylinder_peak_angle(void)
{
    if (fuya_cylinder_peak_flag)
    {
        app.angle.A_1 = fuya_angle_backup_a1;
        app.angle.B_1 = fuya_angle_backup_b1;
        app.angle.C_l = fuya_angle_backup_cl;
    }

    fuya_cylinder_top_count = 0;
    fuya_cylinder_ground_count = 0;
    fuya_cylinder_peak_flag = 0;
}

/**
 * @brief 负压模块初始化
 * @details 初始化 PWM 通道并复位内部状态到安全默认值。
 */
void fuya_Init(void)
{
    pwm_init(PWMA_CH2N_P03, 50, 0);

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
 * @details 该接口用于菜单或调参命令写入；停机后首次拉起采用软启动阶梯上升。
 */
void fuya_set_percent(uint8 percent)
{
    uint8 limited_percent;
    int pwm_value;
    int output_pwm;

    /* 菜单/上位机统一按百分比输入，内部转换为 ESC 脉宽 */
    limited_percent = fuya_limit_percent((int)percent);
    pwm_value = fuya_percent_to_pwm(limited_percent);
    output_pwm = fuya_ramp_pwm_startup(fuya_date, pwm_value);

    fuya_target_percent = limited_percent;
    fuya_target_pwm = pwm_value;
    fuya_date = output_pwm;
    fuya_motor_output(output_pwm);
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
    fuya_startup_ramp_active = 1;
    fuya_restore_cylinder_peak_angle();
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
    vzc = fuya_read_vzc();

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
    output_pwm = fuya_ramp_pwm_startup(fuya_date, target_pwm);

    fuya_target_percent = target_percent;
    fuya_target_pwm = target_pwm;
    fuya_date = output_pwm;

    fuya_motor_output(output_pwm);
}

/**
 * @brief 10ms 圆筒最高点检测与角度切换
 * @details
 * 判定逻辑为：检测车体 Z 轴在圆筒顶部连续进入负值极限区间，
 * 以此确认已到达圆筒最高点附近。回到平地后自动恢复参数。
 */
void fuya_update_cylinder_peak_10ms(int8 start_state)
{
    float vzc;

    if (2 != start_state)
    {
        fuya_restore_cylinder_peak_angle();
        return;
    }

    vzc = fuya_read_vzc();

    if (!fuya_cylinder_peak_flag)
    {
        if (vzc <= FUYA_CYLINDER_TOP_VZ)
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

    if (vzc >= FUYA_CYLINDER_VZ)
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
