#ifndef __PID_H__
#define __PID_H__
#include "zf_common_headfile.h"
// 速度环 PID（增量式），字段说明
typedef struct
{
  float Kp;          // 比例系数
  float Ki;          // 积分系数
  float Kd;          // 微分系数
  float error;       // 当前误差
  float prev_error;  // 上一次误差
  float prev2_error; // 上上次误差
  float output;      // 当前输出值
  float max_output;  // 输出限幅值
  float min_output;  // 输出限幅值
  float speed;       // 当前速度
} PID_Speed;
// 转向环 PID（位置式），字段说明
typedef struct
{
  float Kp;           // 比例系数
  float Kd;           // 微分系数
  float Kp2; // 陀螺仪微分系数（陀螺项权重）
  float error;        // 当前误差
  float prev_error;   // 上一次误差
  float output;       // 当前输出值
  float max_output;   // 输出限幅值
  float min_output;   // 输出限幅值
} PID_Steer;
// 控制器聚合：左右速度环 + 转向环
typedef struct
{
  PID_Speed left_speed;  // 左轮速度环
  PID_Speed right_speed; // 右轮速度环
  PID_Steer steer;       // 转向（位置式）环
  PID_Steer angle;       // 角度（位置式）环
} PID_Controllers;

// 初始化函数：速度环（增量式）与转向环（位置式）
void pid_speed_init(PID_Speed *pid, float kp, float ki, float kd, float max_out, float min_out);
void pid_steer_init(PID_Steer *pid, float kp, float kd, float Kp2, float max_out, float min_out);
void Encoder_get(PID_Speed *left, PID_Speed *right);
void pid_speed_update(PID_Speed *pid, float target, float actual);
void pid_steer_update(PID_Steer *pid, float error);
void pid_angle_update(PID_Steer *pid, float error, float gyro);
void Pid_Differential(float speed_run, float *left_target, float *right_target, float Scope);
void Pure_Pursuit_Control(float speed_ref, float norm_error, float *left_target, float *right_target);
void Pure_Pursuit_Gyro_Control(float speed_ref, float norm_error, float gyro_z, float *left_target, float *right_target);
extern float speed_l, speed_r;
extern PID_Controllers PID;

#endif
