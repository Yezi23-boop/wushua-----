#ifndef _IMU_H_
#define _IMU_H_
extern float gyro_z;
/*
 * 模块说明
 * - 提供 IMU 姿态解算相关的类型、常量与接口声明；
 * - 与实现文件 imu.c 配合使用：先调用 Prepare_Data() 读取/预处理传感器，
 *   再调用 IMUupdate() 完成一次姿态更新，输出滚俯偏 Att_Angle。
 *
 * 坐标/单位约定
 * - 角度：输出单位为“度”。
 * - 角速度：Gyr_* 使用“弧度/秒”（rad/s）；若为“度/秒”，请先乘以 DegtoRad。
 * - 加速度：单位由底层转换函数 imu660ra_acc_transition 决定（g 或 m/s^2），
 *   姿态解算仅使用方向，内部会做归一化。
 */

/* 角度/弧度转换常量
 * RadtoDeg：弧度 -> 度 的比例系数（项目中约定为 57.324841f）
 * DegtoRad：度   -> 弧度 的比例系数（约 0.0174533f）
 */
#define RadtoDeg 57.324841f
#define DegtoRad 0.0174533f

/* 三轴浮点（通用 XYZ） */
typedef struct
{
    float X; // X 轴分量
    float Y; // Y 轴分量
    float Z; // Z 轴分量
} FLOAT_XYZ;

/* 姿态角（单位：度）
 * rol：横滚（Roll，对应 X 轴）
 * pit：俯仰（Pitch，对应 Y 轴）
 * yaw：偏航（Yaw，对应 Z 轴）
 */
typedef struct
{
    float rol;
    float pit;
    float yaw;
} FLOAT_ANGLE;

/* 四元数（全局，由 imu.c 内部管理）
 * - q0 为标量部，q1~q3 为向量部；
 * - 由 IMUupdate() 更新，外部可只读使用。
 */
extern float q0, q1, q2, q3;

/* 全局姿态角输出（度） */
extern FLOAT_ANGLE Att_Angle;

/* 中间量（方向余弦矩阵与误差等，便于调试查看）
 * - vx,vy,vz：当前四元数对应的重力方向（机体坐标系下）
 * - ex,ey,ez：重力方向的偏差误差
 * - norm    ：归一化中间结果
 */
extern float vx, vy, vz, ex, ey, ez, norm;

/* 预处理后的传感器量
 * - Acc_filt：滤波后的加速度（仅方向用于姿态校正）
 * - Gyr_filt：滤波后的角速度（单位：rad/s）
 */
extern FLOAT_XYZ Acc_filt, Gyr_filt;

/* 快速 1/sqrt(x)
 * - 近似算法：速度快，精度对姿态解算已足够；
 * - 若在其它模块调用请关注其近似特性；项目中一般只在 imu.c 内部使用。
 */
float invSqrt(float x);

/* 快速 sqrt(x)
 * - 近似算法：速度快，精度对姿态解算已足够；
 * - 若在其它模块调用请关注其近似特性；项目中一般只在 imu.c 内部使用。
 */
float SquareRootFloat(float number);

/* 传感器数据预处理
 * 功能：
 * - 调用底层驱动读取一帧 IMU 原始数据；
 * - 完成单位转换与零偏校正；
 * - 更新全局 Acc_filt、Gyr_filt；
 * 先决条件：
 * - 已正确初始化 IMU 硬件与驱动；
 * - 提供 Gyro_offset_x/y/z（零偏）与 DegtoRad 常量；
 */
void Prepare_Data(void);

/* 姿态更新（主函数）
 * 参数：
 * - Gyr_rad  ：角速度（三轴，单位 rad/s）
 * - Acc_filt ：加速度（三轴，仅方向，内部先归一化）
 * - Att_Angle：输出姿态角（度）
 * 原理：
 * - 基于四元数的 PI 互补融合（加速度作为重力方向校正）；
 * - 内部管理四元数 q0~q3，并转换输出滚俯偏角度；
 * 调用频率与步长：
 * - 需与实际采样周期匹配（imu.c 中的 halfT/分频步长应对应实际 Ts）。
 */
void IMUupdate(FLOAT_XYZ *Gyr_rad, FLOAT_XYZ *Acc_filt, FLOAT_ANGLE *Att_Angle);

#endif
