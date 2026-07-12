#ifndef _IMU_H_
#define _IMU_H_

#include "zf_common_typedef.h"

/**
 * @file imu.h
 * @brief IMU 姿态辅助接口声明
 * @details
 * 对外提供角速度更新与基础数学工具函数，供主控制环复用。
 */

extern volatile float gyro_z; /* 当前 Z 轴角速度反馈量 (中断与主循环共享) */

/**
 * @brief 上电标定 gyro_z 零偏
 * @details 在静止状态下采样均值，用于后续去零飘。
 */
void imu_calibrate_gyro_z_zero_drift(void);

/**
 * @brief 更新并计算用于控制的 Z 轴角速度率 (gyro_z)
 *
 * @details
 * 从 imu660rc 硬件驱动中读取当前 z 轴角速度原始值，按预定系数转换为
 * 带工程量纲的 float 数据，并消除上电记录的零偏。
 *
 * @note 必须在高频入口 (2ms) 更新，提供给转向差速阻尼项使用。
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
