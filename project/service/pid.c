#include "pid.h"

///* 左右轮编码器组合滤波状态：符号纠错 + 3 点中值 + EMA(1/2) */
// static EncoderMedian3EmaFilterState encoder_filter_left;
// static EncoderMedian3EmaFilterState encoder_filter_right;
LowPassFilter_t encoder_filter_left;
LowPassFilter_t encoder_filter_right;
/* 内部中间变量 */
float speed_l = 0; /* 左轮当前速度反馈 */
float speed_r = 0; /* 右轮当前速度反馈 */

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
 * @details 读取硬件编码器计数值，执行组合滤波后再转换为速度
 * @param left 左轮 PID 结构指针
 * @param right 右轮 PID 结构指针
 */
void Encoder_get(PID_Speed *left, PID_Speed *right)
{
    //    int32 fixed_left_count;
    //    int32 fixed_right_count;

    //    fixed_left_count = FilterEncoderCountMedian3EmaHalf((int32)encoder_get_count(TIM4_ENCOEDER),
    //                                                        &encoder_filter_left);
    //    fixed_right_count = FilterEncoderCountMedian3EmaHalf(-(int32)encoder_get_count(TIM3_ENCOEDER),
    //                                                         &encoder_filter_right);
    speed_r = -(int32)encoder_get_count(TIM4_ENCOEDER) * 0.175f;
    speed_l = (int32)encoder_get_count(TIM3_ENCOEDER) * 0.175f;
    if (speed_l < 0)
    {
        speed_l = -speed_l;
    }
    if (speed_r < 0)
    {
        speed_r = -speed_r;
    }
    low_pass_filter_mt(&encoder_filter_left, &speed_l, 0.25f);
    low_pass_filter_mt(&encoder_filter_right, &speed_r, 0.25f);
    //    speed_l = (float)fixed_left_count * 0.2f;
    //    speed_r = (float)fixed_right_count * 0.2f;

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

    // 更新输出并限幅
    //   pid->output += delta_output;
    if (pid->output > pid->max_output)
    {
        pid->output = pid->max_output;
    }
    else if (pid->output < -pid->max_output)
    {
        pid->output = -pid->max_output;
    }

    // 更新误差历史
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
 * @details 将转向控制器的输出转化为左右轮的目标速度差
 * @param speed_run 基础运行速度（直道速度）
 * @param left_target 输出：左轮目标速度
 * @param right_target 输出：右轮目标速度
 * @param Scope 差速系数映射范围（通常根据赛道宽度和车体特性标定）
 */
void Pid_Differential(float speed_run, float *left_target, float *right_target, float Scope)
{
    float k;
    float delta = PID.steer.output; /* 获取当前转向差速控制输出 */

    /* Scope 作为教程版 eleOut->k 的归一化范围，默认按 -100~100 处理 */
    if (Scope < 0.001f)
        Scope = 100.0f;

    k = delta / Scope;

    /* 教程版差速限幅：将 k 限制在 -0.65 ~ 0.65，避免转向过猛 */
    if (k > 0.65f)
        k = 0.65f;
    else if (k < -0.65f)
        k = -0.65f;

    if (k >= 0.0f) /* 左转：左轮减速更多，右轮只做小幅补偿 */
    {
        *left_target = speed_run * (1.0f - k);
        *right_target = speed_run * (1.0f + k * 0.2f);
    }
    else /* 右转：右轮减速更多，左轮只做小幅补偿 */
    {
        k = -k;

        *left_target = speed_run * (1.0f + k * 0.2f);
        *right_target = speed_run * (1.0f - k);
    }
}
