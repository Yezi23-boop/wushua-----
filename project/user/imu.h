#ifndef _IMU_H_
#define _IMU_H_

#include "zf_common_typedef.h"

/**
 * @file imu.h
 * @brief IMU 姿态辅助接口声明
 * @details
 * 对外提供重力向量计算、角速度更新与基础数学工具函数，
 * 供主控制环、负压控制与调试链路复用。
 */

extern float gyro_z; /* 当前 Z 轴角速度反馈量 */

/**
 * @brief 上电标定 gyro_z 零偏
 * @details 在静止状态下采样均值，用于后续去零飘。
 */
void imu_calibrate_gyro_z_zero_drift(void);

/**
 * @brief 由四元数更新重力向量
 * @param vx 输出重力向量 X 分量，可为 0 表示不需要
 * @param vy 输出重力向量 Y 分量，可为 0 表示不需要
 * @param vz 输出重力向量 Z 分量，可为 0 表示不需要
 */
void imu_update_gravity_vector_from_quaternion(float *vx, float *vy, float *vz);

/**
 * @brief 更新全局 gyro_z
 * @details 从 imu660rc 原始陀螺仪数据转换得到控制环使用量纲。
 */
void imu_update_gyro_z_from_imu660rc(void);

/**
 * @brief C89 兼容 atan2
 */
double my_atan2(double y, double x);

/**
 * @brief 快速反平方根近似
 */
float invSqrt(float x);

/**
 * @brief 快速平方根近似
 */
float SquareRootFloat(float number);

#endif /* _IMU_H_ */
