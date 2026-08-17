#include "pid.h"

LowPassFilter_t encoder_filter_left;
LowPassFilter_t encoder_filter_right;
/* 内部中间变量 */
float speed_l = 0;        /* 左轮当前速度反馈，带符号，经低通滤波后供普通速度环使用。 */
float speed_r = 0;        /* 右轮当前速度反馈，带符号，经低通滤波后供普通速度环使用。 */
float speed_l_signed = 0; /* 左轮带符号速度反馈（未滤波），跷跷板零速刹车和里程方向判断使用。 */
float speed_r_signed = 0; /* 右轮带符号速度反馈（未滤波），跷跷板零速刹车和里程方向判断使用。 */

/* 实例化全局控制器聚合结构 */
PID_Controllers PID;

/**
 * @brief 速度环初始化（增量式）
 * @param pid PID结构体指针
 * @param kp 比例系数
 * @param ki 积分系数
 * @param kd 微分系数
 * @param max_out 正向输出限幅
 * @param min_out 反向输出限幅
 */
void pid_speed_init(PID_Speed *pid, float kp, float ki, float kd, float max_out, float min_out)
{
    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
    pid->error = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev2_error = 0.0f;
    pid->output = 0.0f;
    pid->speed = 0.0f;
    pid->max_output = max_out;
    pid->min_output = min_out;
}

/**
 * @brief 清零速度环运行时状态（增量式 PID 历史误差和输出）
 *
 * 在元素切换（跷跷板刹车/恢复、圆环进出等）或状态机复位时调用，
 * 避免上一段控制残差（error/prev_error/prev2_error/output）影响新阶段响应。
 * 不清除 Kp/Ki/Kd 和限幅参数，仅复位运行时累积量。
 *
 * @param pid 速度环 PID 结构指针，指向 PID.left_speed 或 PID.right_speed。
 */
void pid_speed_reset(PID_Speed *pid)
{
    pid->error = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev2_error = 0.0f;
    pid->output = 0.0f;
    pid->speed = 0.0f;
}

/**
 * @brief 转向环初始化（位置式）
 * @param pid PID结构体指针
 * @param kp 比例系数
 * @param kd 微分系数
 * @param Kp2 增强项/非线性系数
 * @param max_out 正向输出限幅
 * @param min_out 反向输出限幅
 */
void pid_steer_init(PID_Steer *pid, float kp, float kd, float Kp2, float gyro_damp, float max_out, float min_out)
{
    pid->Kp = kp;
    pid->Kd = kd;
    pid->Kp2 = Kp2;
    pid->gyro_damp = gyro_damp;
    pid->error = 0.0f;
    pid->prev_error = 0.0f;
    pid->output = 0.0f;
    pid->max_output = max_out;
    pid->min_output = min_out;
}

/**
 * @brief 读取并处理编码器数据
 * @details 读取硬件编码器计数值，执行低通滤波后再转换为速度
 * @param left 左轮 PID 结构指针
 * @param right 右轮 PID 结构指针
 */
void Encoder_get(PID_Speed *left, PID_Speed *right)
{
    static float encoder_sum = 0.0f; /* 上电后左右轮平均累计里程，单位沿用项目标尺 cm。 */
    /* 编码器脉冲→速度转换系数 0.175f：轮周长(cm) / 编码器线数 / 减速比 / 采样周期(s)，
     * 需根据实际硬件标定。右轮取反是因为编码器安装方向与左轮相反。 */
    speed_r_signed = (int32)encoder_get_count(TIM4_ENCOEDER) * 0.175f;
    speed_l_signed = -(int32)encoder_get_count(TIM3_ENCOEDER) * 0.175f;
    speed_r = speed_r_signed;
    speed_l = speed_l_signed;
//    if (speed_l < 0)
//    {
//        speed_l = -speed_l;
//    }
//    if (speed_r < 0)
//    {
//        speed_r = -speed_r;
//    }
    /* 低通滤波 alpha=0.25f：一阶 IIR 滤波器系数，y[n]=alpha*x[n]+(1-alpha)*y[n-1]。
     * 0.40 约对应 2ms 周期下 ~5ms 的阶跃响应时间常数，平衡响应速度与平滑度。 */
    low_pass_filter_mt(&encoder_filter_left, &speed_l, 0.40f);
    low_pass_filter_mt(&encoder_filter_right, &speed_r, 0.40f);

    encoder_sum += (speed_l + speed_r) * 0.5f * 0.012f;
    if (encoder_sum >= app.start.encoder_stop_distance_cm)
    {
        stop = 1;
    }

    left->speed = speed_l;
    right->speed = speed_r;

    /* 清零硬件计数器，准备下一采样周期的计数 */
    encoder_clear_count(TIM3_ENCOEDER);
    encoder_clear_count(TIM4_ENCOEDER);
}

/**
 * @brief 速度环 PID 更新（增量式算法）
 * @details 公式：delta_u = Kp*(e(k)-e(k-1)) + Ki*e(k) + Kd*(e(k)-2*e(k-1)+e(k-2))
 * @param pid PID 结构指针
 * @param target 目标速度
 * @param actual 实际反馈速度
 */
void pid_speed_update(PID_Speed *pid, float target, float actual)
{
    float delta_output;

    /* 全浮点增量式计算，确保 Kp/Ki/Kd 的小数调节可以实时生效 */
    pid->error = target - actual;

    delta_output = pid->Kp * (pid->error - pid->prev_error) + pid->Ki * pid->error + pid->Kd * (pid->error - 2.0f * pid->prev_error + pid->prev2_error);
    pid->output += delta_output;

    if (pid->output > pid->max_output)
    {
        pid->output = pid->max_output;
    }
    else if (pid->output < -pid->max_output)
    {
        pid->output = -pid->max_output;
    }

    pid->prev2_error = pid->prev_error;
    pid->prev_error = pid->error;
}

/**
 * @brief 转向环 PID 更新（位置式算法，支持非线性增强）
 * @details 用于基于电感偏差的转向控制
 * @param pid PID 结构指针
 * @param error 当前位置偏差（通常来自电感归一化计算）
 * @param gyro_feedback 当前 gyro_z 反馈量，用于增加转向阻尼
 */
void pid_steer_update(PID_Steer *pid, float error, float gyro_feedback)
{
    pid->error = error;

    /*
     * 位置式 PID 计算：
     * 包含比例项、非线性项（error * |error|）、误差微分项和 gyro 阻尼项
     * 非线性项用于在误差较大时提供更强的回归力
     */
    pid->output = pid->Kp * pid->error +
                  pid->Kp2 * error * func_abs(error) +
                  pid->Kd * (pid->error - pid->prev_error) -
                  pid->gyro_damp * gyro_feedback;

    /* 输出限幅 */
    if (pid->output > pid->max_output)
    {
        pid->output = pid->max_output;
    }
    else if (pid->output < -pid->min_output)
    {
        pid->output = -pid->min_output;
    }

    /* 记录误差用于下次微分计算 */
    pid->prev_error = pid->error;
}

/**
 * @brief 角速度环 PID 更新（位置式算法）
 * @details Uses the calibrated gyro_z feedback to suppress yaw oscillation or support turn control
 * @param pid PID 结构指针
 * @param error 目标偏差（通常是 目标角速度 - 当前角速度）
 * @param gyro Calibrated steering feedback value
 */
void pid_angle_update(PID_Steer *pid, float error, float gyro)
{
    /* 计算综合偏差 */
    pid->error = error - gyro;

    /* 位置式 PD 控制 */
    pid->output = pid->Kp * pid->error + pid->Kd * (pid->error - pid->prev_error);

    /* 限幅处理 */
    if (pid->output > pid->max_output)
    {
        pid->output = pid->max_output;
    }
    else if (pid->output < -pid->min_output)
    {
        pid->output = -pid->min_output;
    }

    pid->prev_error = pid->error;
}

/**
 * @brief 差速分配函数
 * @details 大弯主要降低内轮；负压提供额外抓地力，允许外轮小幅增速以保持转弯力度。
 * @param speed_run 基础运行速度（直道速度）
 * @param diff_output 角速度内环输出的差速控制量
 * @param left_target 输出：左轮目标速度
 * @param right_target 输出：右轮目标速度
 * @param scope 差速归一化范围，正式控制链传入角速度内环限幅
 * @param inner_gain 内轮减速增益
 * @param outer_gain 外轮增速增益
 */
void Pid_Differential(float speed_run, float diff_output,
                      float *left_target, float *right_target,
                      float scope, float inner_gain, float outer_gain)
{
    float ratio;
    float inner_scale;
    float outer_scale;

    if (scope < 1.0f)
    {
        scope = 1.0f;
    }

    ratio = func_abs(diff_output) / scope;
    if (ratio > 1.0f)
    {
        ratio = 1.0f;
    }

    /* 小弯保持柔和，大弯快速增强内外轮差速。 */
    ratio = ratio * (0.6f + 0.4f * ratio);
    inner_scale = 1.0f - inner_gain * ratio;
    outer_scale = 1.0f + outer_gain * ratio;

    if (diff_output >= 0.0f)
    {
        *left_target = speed_run * inner_scale;
        *right_target = speed_run * outer_scale;
    }
    else
    {
        *left_target = speed_run * outer_scale;
        *right_target = speed_run * inner_scale;
    }
}
