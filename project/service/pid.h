#ifndef __PID_H__
#define __PID_H__

#include "zf_common_headfile.h"

/**
 * @brief 速度环 PID 结构体（增量式）
 * @details 用于左右轮的速度闭环控制，采用增量式 PID 算法减少积分饱和影响
 */
typedef struct
{
    float Kp;          /**< 比例系数：响应速度，过大易震荡 */
    float Ki;          /**< 积分系数：消除静差，过大易超调 */
    float Kd;          /**< 微分系数：抑制震荡，增强系统稳定性 */
    float error;       /**< 当前误差：目标值 - 实际值 */
    float prev_error;  /**< 上一次误差：用于计算微分项和比例增量 */
    float prev2_error; /**< 上上次误差：用于增量式 PID 计算 */
    float output;      /**< 当前输出值：PWM 占空比或目标电流 */
    float max_output;  /**< 输出上限限幅值 */
    float min_output;  /**< 输出下限限幅值 */
    float speed;       /**< 当前实时速度：由编码器反馈计算得到 */
} PID_Speed;

/**
 * @brief 转向差速 PID 结构体（位置式）
 * @details 用于基于电感偏差生成左右轮差速量，支持传统 PD 和增强型非线性项
 */
typedef struct
{
    float Kp;         /**< 比例系数：主控项 */
    float Kd;         /**< 微分系数：阻尼项，抑制转向过冲 */
    float Kp2;        /**< 增强项系数：用于非线性控制或陀螺仪前馈 */
    float gyro_damp;  /**< 陀螺仪阻尼系数：用于抑制高速摆振 */
    float error;      /**< 当前误差：赛道偏差或角度偏差 */
    float prev_error; /**< 上一次误差：计算微分项 */
    float output;     /**< 当前输出值：通常作为差速叠加量 */
    float max_output; /**< 输出上限限幅值 */
    float min_output; /**< 输出下限限幅值 */
} PID_Steer;

/**
 * @brief 全局控制器聚合结构体
 * @details 统一管理小车所有的 PID 控制器实例
 */
typedef struct
{
    PID_Speed left_speed;  /**< 左轮速度环控制器 */
    PID_Speed right_speed; /**< 右轮速度环控制器 */
    PID_Steer steer;       /**< 转向差速控制器（基于电感偏差） */
    PID_Steer angle;       /**< 角速度内环控制器（跟踪转向目标角速度） */
} PID_Controllers;

/* --- 函数声明 --- */

/**
 * @brief 初始化速度环 PID 参数
 */
void pid_speed_init(PID_Speed *pid, float kp, float ki, float kd, float max_out, float min_out);

/**
 * @brief 清零速度环运行时状态
 */
void pid_speed_reset(PID_Speed *pid);

/**
 * @brief 初始化转向环 PID 参数
 */
void pid_steer_init(PID_Steer *pid, float kp, float kd, float Kp2, float gyro_damp, float max_out, float min_out);

/**
 * @brief 读取编码器数据并更新到 PID 结构体中
 */
void Encoder_get(PID_Speed *left, PID_Speed *right);

/**
 * @brief 更新速度环 PID 计算（增量式）
 */
void pid_speed_update(PID_Speed *pid, float target, float actual);

/**
 * @brief 更新转向环 PID 计算（位置式）
 */
void pid_steer_update(PID_Steer *pid, float error, float gyro_feedback);

/**
 * @brief 更新角速度内环 PID 计算（位置式）
 * @param pid PID 结构指针
 * @param error 目标角速度
 * @param gyro 当前角速度反馈
 */
void pid_angle_update(PID_Steer *pid, float error, float gyro);

/**
 * @brief 差速分配逻辑
 * @param speed_run 基础运行速度
 * @param diff_output 角速度内环输出的差速控制量
 * @param left_target 输出：左轮目标速度指针
 * @param right_target 输出：右轮目标速度指针
 * @param scope 差速归一化范围
 */
void Pid_Differential(float speed_run, float diff_output, float *left_target, float *right_target, float scope);

/**
 * @brief 纯追踪算法控制（电感偏差驱动）
 */
void Pure_Pursuit_Control(float speed_ref, float norm_error, float *left_target, float *right_target);

/* --- 全局变量外部声明 --- */
extern float speed_l, speed_r;
extern float speed_l_signed, speed_r_signed;
extern PID_Controllers PID;

#endif /* __PID_H__ */
