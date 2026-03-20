#include "pid.h"

/* 实例化全局控制器聚合结构 */
PID_Controllers PID; 

/* 编码器低通滤波器实例 */
LowPassFilter_t encoder_l;
LowPassFilter_t encoder_r;

/* 内部中间变量 */
float delta_output = 0;   /* 增量式 PID 计算出的输出增量 */
float speed_l = 0;        /* 左轮当前平滑速度 */
float speed_r = 0;        /* 右轮当前平滑速度 */
float max_integral = 0;   /* 预留：积分限幅（当前增量式未直接使用） */

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
void pid_steer_init(PID_Steer *pid, float kp, float kd, float Kp2, float max_out, float min_out)
{
    pid->Kp = kp;
    pid->Kd = kd;
    pid->prev_error = 0.0f;
    pid->output = 0.0f;
    pid->max_output = max_out;
    pid->min_output = min_out;
    pid->Kp2 = Kp2;
    pid->error = 0.0f;
}

/**
 * @brief 读取并处理编码器数据
 * @details 读取硬件编码器计数值，转换为物理速度，并进行低通滤波处理
 * @param left 左轮 PID 结构指针
 * @param right 右轮 PID 结构指针
 */
void Encoder_get(PID_Speed *left, PID_Speed *right)
{
    /* 
     * 读取硬件编码器计数值 
     * 乘以 0.2f 是将原始计数值转换为实际速度单位的缩放因子
     * 注意：左轮和右轮可能因为安装方向不同而需要取反
     */
    right->speed = encoder_get_count(TIM4_ENCOEDER) * 0.2f; /* 右电机编码器 */
    left->speed = -encoder_get_count(TIM3_ENCOEDER) * 0.2f;    /* 左电机编码器 */

    /* 对原始速度进行一阶低通滤波，减小编码器噪声对速度环的影响 */
    low_pass_filter_mt(&encoder_l, &left->speed, 0.8f); /* alpha=1.0 代表暂不滤波，可根据需要调整 */
    low_pass_filter_mt(&encoder_r, &right->speed, 0.8f);

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
    /* 计算当前偏差 */
    pid->error = target - actual;

    /* 增量式 PID 公式计算输出增量 */
    delta_output = pid->Kp * (pid->error - pid->prev_error) + 
                   pid->Ki * pid->error + 
                   pid->Kd * (pid->error - 2.0f * pid->prev_error + pid->prev2_error);

    /* 累加增量到当前输出值 */
    pid->output += delta_output;

    /* 输出限幅保护 */
    if (pid->output > pid->max_output)
    {
        pid->output = pid->max_output;
    }
    else if (pid->output < -pid->max_output)
    {
        pid->output = -pid->max_output;
    }

    /* 更新误差历史，供下一周期使用 */
    pid->prev2_error = pid->prev_error;
    pid->prev_error = pid->error;
}

/**
 * @brief 转向环 PID 更新（位置式算法，支持非线性增强）
 * @details 用于基于电感偏差的转向控制
 * @param pid PID 结构指针
 * @param error 当前位置偏差（通常来自电感归一化计算）
 */
void pid_steer_update(PID_Steer *pid, float error)
{
    pid->error = error;

    /* 
     * 位置式 PID 计算：
     * 包含比例项、非线性项（error * |error|）和微分项
     * 非线性项用于在误差较大时提供更强的回归力
     */
    pid->output = pid->Kp * pid->error + 
                  pid->Kp2 * error * func_abs(error) + 
                  pid->Kd * (pid->error - pid->prev_error);

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
 * @brief 角度环 PID 更新（位置式算法）
 * @details Uses the calibrated gyro_z feedback to suppress yaw oscillation or support turn control
 * @param pid PID 结构指针
 * @param error 目标偏差（通常是 目标角度 - 当前角度）
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
    float delta = PID.angle.output; /* 获取角度环/转向环的控制输出 */

    /* 基础防错：防止除零 */
    if (Scope < 0.001f) Scope = 100.0f;

    if (delta >= 0.0f) /* 控制输出为正，通常代表需要向左转 */
    {
        /* 计算归一化差速系数 k */
        k = delta / Scope;
        if (k > 1.0f) k = 1.0f; /* 限幅：最大差速不超过基础速度 */

        /* 差速策略：内侧轮减速，外侧轮适当加速以补偿转弯半径 */
        *left_target = speed_run * (1.0f - k);
        *right_target = speed_run * (1.0f + k * 0.5f);
    }
    else /* 控制输出为负，代表需要向右转 */
    {
        k = -delta / Scope;
        if (k > 1.0f) k = 1.0f;

        *left_target = speed_run * (1.0f + k * 0.5f);
        *right_target = speed_run * (1.0f - k);
    }
}

/* 纯追踪相关常量（建议放入头文件或配置结构体中） */
#define TRACK_WIDTH 80.0f /* 轮距（参考值，单位 cm） */
#define BASE_L 150.0f     /* 基础预瞄距离（参考值，单位 cm） */
#define K_SPEED 0.5f      /* 速度相关的预瞄增益 */

/**
 * @brief 纯追踪 + 陀螺仪闭环混合控制 (Pure Pursuit + Gyro Loop)
 * @details 
 * 1. 利用纯追踪模型计算理论目标曲率和角速度
 * 2. 利用陀螺仪角速度作为反馈，进行内环闭环控制
 * 3. 实现更平滑的高速循迹和抗干扰能力
 * @param speed_ref 基础参考速度
 * @param norm_error 归一化后的赛道位置偏差
 * @param gyro_z Calibrated steering feedback value
 * @param left_target 输出：左轮目标速度
 * @param right_target 输出：右轮目标速度
 */
void Pure_Pursuit_Gyro_Control(float speed_ref, float norm_error, float gyro_z, float *left_target, float *right_target)
{
    float look_ahead_L;
    float curvature;
    float target_omega; /* 目标角速度 */
    float diff_output;  /* 最终差速输出量 */

    /* 1. 计算自适应预瞄距离：随速度增大而增大，提高高速稳定性 */
    look_ahead_L = BASE_L + K_SPEED * speed_ref;

    /* 2. 根据纯追踪几何模型计算曲率：curvature = 2*sin(alpha) / L */
    curvature = (2.0f * norm_error) / look_ahead_L;

    /* 曲率限幅，防止计算出的转向过于剧烈 */
    if (curvature > 0.1f)  curvature = 0.1f;
    if (curvature < -0.1f) curvature = -0.1f;

    /* 3. 将曲率换算成目标转向角速度 */
    /* 保留 57.3f 系数，维持当前工程已有的控制公式量纲 */
    target_omega = speed_ref * curvature * 57.3f;

    /* 4. 使用校准后的 gyro_z 做内环闭环反馈 */
    /* 此处沿用当前工程对 gyro_z 的定义与单位 */
    pid_steer_update(&PID.angle, target_omega - gyro_z);

    /* 5. 获取 PID 控制器的输出作为差速调节量 */
    diff_output = PID.angle.output;

    /* 6. 将调节量叠加到基础速度上，实现差速转向 */
    /* 差速逻辑：左转时 diff_output 为正，左轮减速，右轮加速 */
    *left_target = speed_ref - diff_output;
    *right_target = speed_ref + diff_output;
}
