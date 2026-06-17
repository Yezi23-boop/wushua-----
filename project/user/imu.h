#ifndef _IMU_H_
#define _IMU_H_

#include "zf_common_typedef.h"

/**
 * @file imu.h
 * @brief IMU 姿态辅助接口声明
 * @details
 * 对外提供 roll 角差缓存、pitch 短历史缓存、角速度更新与基础数学工具函数，
 * 供主控制环、负压控制与调试链路复用。
 */

extern volatile float gyro_z; /* 当前 Z 轴角速度反馈量 (中断与主循环共享) */
extern float acc_1;

#define IMU_PITCH_WALL_WINDOW_MS 500u /* 墙面 pitch 判定窗口，现场可在 0~1000ms 内快速修改。 */

/**
 * @brief 上电标定 gyro_z 零偏
 * @details 在静止状态下采样均值，用于后续去零飘。
 */
void imu_calibrate_gyro_z_zero_drift(void);

/**
 * @brief 更新 roll 角差与 pitch 短历史缓存
 * @details 由 2ms 主控制链路调用一次，供墙面识别、负压、圆桶和调试显示读取。
 */
void imu_update_gravity_vz_from_roll(void);

/**
 * @brief 读取最近一次更新的 roll 角差
 * @return float 已折回到 -180.0f~180.0f 的 roll 角差，单位：度；平地约 0。
 */
float imu_get_gravity_vz(void);

/**
 * @brief 读取最近一次 2ms 缓存的 pitch 角。
 * @return int16 pitch * 10，单位为 0.1 度。
 */
int16 imu_get_pitch_current_x10(void);

/**
 * @brief 读取墙面 pitch 判定窗口前的 pitch 角。
 *
 * 墙面确认用该值作为“电感连续命中时刻之前”的姿态基准，避免直接依赖负压开启后的绝对角度。
 *
 * @return int16 pitch * 10，单位为 0.1 度。
 * @note 依赖主控制环每 2ms 调用姿态缓存更新。
 */
int16 imu_get_pitch_wall_window_ago_x10(void);

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
