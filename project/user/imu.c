#include "zf_common_headfile.h"
#include "math.h"
#include "imu.h"
#include "../service/filter.h"

/* --- 全局变量定义 --- */
volatile float gyro_z = 0;
float Gyro_offset_x = 0, Gyro_offset_y = 0, Gyro_offset_z = 0;
float acc_offset_x = 0, acc_offset_y = 0, acc_offset_z = 0;
int imu_flat_star = 0; /* 零偏初始化完成标志 */

/* Mahony 算法参数：Kp 控制收敛速度，Ki 控制静差补偿 */
#define Kp 5.0f
#define Ki 0.007f

/* 采样周期相关：halfT = 0.5 * Ts */
/* 若 IMUupdate 周期为 10ms，则 halfT = 0.005f */
#define halfT 0.005f

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

/* 实例对象 */
FLOAT_ANGLE Att_Angle;
FLOAT_XYZ Acc_filt;
FLOAT_XYZ Gyr_filt;
LowPassFilter_t Gyr_filt_lowpass;

/* 四元数状态向量，初始为单位四元数 */
float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
float vx, vy, vz, ex, ey, ez, norm;

/**
 * @brief 自定义 atan2 算法
 */
double my_atan2(double y, double x)
{
    if (x == 0.0)
    {
        if (y > 0.0)
            return M_PI / 2.0;
        if (y < 0.0)
            return -M_PI / 2.0;
        return 0.0;
    }
    else
    {
        double theta = atan(y / x);
        if (x > 0.0)
            return theta;
        else
        {
            if (y >= 0.0)
                return theta + M_PI;
            else
                return theta - M_PI;
        }
    }
}

/**
 * @brief 执行 IMU 零偏静态标定
 */
void offset_init(void)
{
    int rt = 50; /* 采样 50 次取均值 */
    int i;

    for (i = 0; i < rt; i++)
    {
        imu660rc_get_gyro();
        imu660rc_get_acc();

        /* 累加原始物理量 */
        Gyro_offset_x += imu660rc_gyro_transition(imu660rc_gyro_x);
        Gyro_offset_y += imu660rc_gyro_transition(imu660rc_gyro_y);
        Gyro_offset_z += imu660rc_gyro_transition(imu660rc_gyro_z);
        acc_offset_x += imu660rc_acc_transition(imu660rc_acc_x);
        acc_offset_y += imu660rc_acc_transition(imu660rc_acc_y);
        acc_offset_z += imu660rc_acc_transition(imu660rc_acc_z);

        system_delay_ms(5);
    }

    /* 计算均值作为零偏 */
    Gyro_offset_x /= (float)rt;
    Gyro_offset_y /= (float)rt;
    Gyro_offset_z /= (float)rt;
    acc_offset_x /= (float)rt;
    acc_offset_y /= (float)rt;
    acc_offset_z /= (float)rt;

    imu_flat_star = 1;
}

/**
 * @brief 传感器数据预处理
 * @details 将原始数据减去零偏，并转换为弧度/s 或 g
 */
void Prepare_Data(void)
{
    /* 硬件四元数验证阶段：暂停软件姿态链，避免与 INT1 回调重复采样 */
}

/**
 * @brief Mahony 姿态更新算法
 * @details 利用加速度计修正四元数漂移，并积分角速度得到实时姿态
 */
void IMUupdate(FLOAT_XYZ *Gyr_rad, FLOAT_XYZ *Acc, FLOAT_ANGLE *Angle)
{
    (void)Gyr_rad;
    (void)Acc;
    (void)Angle;
    /* 硬件四元数验证阶段：暂停 Mahony 解算，直接观察 IMU660RC 原生输出 */
}

/**
 * @brief 快速平方根倒数 (Quake Fast Inverse Sqrt)
 */
float invSqrt(float x)
{
    float halfx = 0.5f * x;
    float y = x;
    long i = *(long *)&y;
    i = 0x5f375a86 - (i >> 1);
    y = *(float *)&i;
    y = y * (1.5f - (halfx * y * y));
    return y;
}

/**
 * @brief 快速平方根算法
 */
float SquareRootFloat(float number)
{
    long i;
    float x, y;
    x = number * 0.5F;
    y = number;
    i = *(long *)&y;
    i = 0x5f3759df - (i >> 1);
    y = *(float *)&i;
    y = y * (1.5F - (x * y * y));
    return number * y;
}
