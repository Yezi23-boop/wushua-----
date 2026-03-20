#ifndef _IMU_H_
#define _IMU_H_

#include "zf_common_typedef.h"

/**
 * @brief IMU 模块说明
 * @details
 * - 提供 6 轴惯性测量单元（IMU660RA）的数据读取与姿态解算。
 * - 采用 Mahony 互补滤波算法，输出欧拉角（Roll, Pitch, Yaw）。
 * - 坐标系约定：X 前、Y 左、Z 上（符合右手定则）。
 */

/* --- 转换常量 --- */
#define RadtoDeg 57.324841f /**< 弧度转角度系数 */
#define DegtoRad 0.0174533f /**< 角度转弧度系数 */

/* --- 数据结构定义 --- */

/**
 * @brief 三轴浮点坐标结构
 */
typedef struct
{
    float X;
    float Y;
    float Z;
} FLOAT_XYZ;

/**
 * @brief 姿态角结构体（欧拉角）
 */
typedef struct
{
    float rol; /**< 横滚角 (Roll) */
    float pit; /**< 俯仰角 (Pitch) */
    float yaw; /**< 偏航角 (Yaw) */
} FLOAT_ANGLE;

/* --- 全局导出变量 --- */
extern volatile float gyro_z;        /**< Calibrated Z-axis control feedback */
extern float q0, q1, q2, q3;         /**< 姿态四元数 */
extern FLOAT_ANGLE Att_Angle;        /**< 全局欧拉角输出 */
extern FLOAT_XYZ Acc_filt, Gyr_filt; /**< 滤波后的加速度与角速度数据 */

/* --- 中间变量声明（用于调试查看） --- */
extern float vx, vy, vz; /**< 重力向量在机体坐标系下的投影 */
extern float ex, ey, ez; /**< 姿态误差项 */

/* --- 函数声明 --- */

/**
 * @brief 初始化 IMU 零偏校准
 * @details 建议在静止状态下调用，采集均值作为静差
 */
void offset_init(void);

/**
 * @brief 准备传感器数据
 * @details 读取原始 ADC，执行零偏补偿与单位转换
 */
void Prepare_Data(void);

/**
 * @brief 姿态解算更新主函数
 * @param Gyr_rad 实时角速度（弧度/s）
 * @param Acc_filt 实时加速度（仅用于方向校正）
 * @param Att_Angle 输出：更新后的欧拉角
 */
void IMUupdate(FLOAT_XYZ *Gyr_rad, FLOAT_XYZ *Acc_filt, FLOAT_ANGLE *Att_Angle);

/**
 * @brief 快速平方根倒数算法
 */
float invSqrt(float x);

/**
 * @brief 快速平方根算法
 */
float SquareRootFloat(float number);

#endif /* _IMU_H_ */
