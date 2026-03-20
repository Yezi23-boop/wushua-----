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
        imu660ra_get_gyro();
        imu660ra_get_acc();

        /* 累加原始物理量 */
        Gyro_offset_x += imu660ra_gyro_transition(imu660ra_gyro_x);
        Gyro_offset_y += imu660ra_gyro_transition(imu660ra_gyro_y);
        Gyro_offset_z += imu660ra_gyro_transition(imu660ra_gyro_z);
        acc_offset_x += imu660ra_acc_transition(imu660ra_acc_x);
        acc_offset_y += imu660ra_acc_transition(imu660ra_acc_y);
        acc_offset_z += imu660ra_acc_transition(imu660ra_acc_z);

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
    if (imu_flat_star == 1)
    {
        imu660ra_get_acc();
        imu660ra_get_gyro();

        /* 角速度：(当前值 - 零偏) * 角度转弧度 */
        Gyr_filt.X = (imu660ra_gyro_transition(imu660ra_gyro_x) - Gyro_offset_x) * DegtoRad;
        Gyr_filt.Y = (imu660ra_gyro_transition(imu660ra_gyro_y) - Gyro_offset_y) * DegtoRad;
        Gyr_filt.Z = (imu660ra_gyro_transition(imu660ra_gyro_z) - Gyro_offset_z) * DegtoRad;

        /* 生成用于控制反馈的校准后 Z 轴角速度，并做一阶低通滤波 */
        gyro_z = (imu660ra_gyro_transition(imu660ra_gyro_z) - Gyro_offset_z) * 0.082f;
        low_pass_filter_mt(&Gyr_filt_lowpass, &gyro_z, 0.6f);

        /* 加速度预处理 */
        Acc_filt.X = imu660ra_acc_transition(imu660ra_acc_x);
        Acc_filt.Y = imu660ra_acc_transition(imu660ra_acc_y);
        Acc_filt.Z = imu660ra_acc_transition(imu660ra_acc_z);
    }
}

/**
 * @brief Mahony 姿态更新算法
 * @details 利用加速度计修正四元数漂移，并积分角速度得到实时姿态
 */
void IMUupdate(FLOAT_XYZ *Gyr_rad, FLOAT_XYZ *Acc, FLOAT_ANGLE *Angle)
{
    float ax = Acc->X, ay = Acc->Y, az = Acc->Z;
    float gx = Gyr_rad->X, gy = Gyr_rad->Y, gz = Gyr_rad->Z;
    static float exInt = 0.0f, eyInt = 0.0f, ezInt = 0.0f;
    float q0_o, q1_o, q2_o, q3_o;
    float temp_vx;

    /* 1. 加速度向量归一化（提取重力方向） */
    norm = invSqrt(ax * ax + ay * ay + az * az);
    ax *= norm;
    ay *= norm;
    az *= norm;

    /* 2. 计算当前四元数推导出的理论重力方向 v = R' * [0,0,1]^T */
    vx = 2.0f * (q1 * q3 - q0 * q2);
    vy = 2.0f * (q0 * q1 + q2 * q3);
    vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

    /* 3. 叉乘误差：理论方向 v 与 实际方向 a 的偏差 */
    ex = (ay * vz - az * vy);
    ey = (az * vx - ax * vz);
    ez = (ax * vy - ay * vx);

    /* 4. 误差积分与比例校正 */
    exInt += ex * Ki;
    eyInt += ey * Ki;
    ezInt += ez * Ki;
    gx += Kp * ex + exInt;
    gy += Kp * ey + eyInt;
    gz += Kp * ez + ezInt;

    /* 5. 四元数微分方程积分更新 */
    q0_o = q0;
    q1_o = q1;
    q2_o = q2;
    q3_o = q3;
    q0 += (-q1_o * gx - q2_o * gy - q3_o * gz) * halfT;
    q1 += (q0_o * gx + q2_o * gz - q3_o * gy) * halfT;
    q2 += (q0_o * gy - q1_o * gz + q3_o * gx) * halfT;
    q3 += (q0_o * gz + q1_o * gy - q2_o * gx) * halfT;

    /* 6. 四元数重新归一化 */
    norm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= norm;
    q1 *= norm;
    q2 *= norm;
    q3 *= norm;

    /* 7. 计算欧拉角（俯仰角、横滚角，以及偏航角） */
    temp_vx = vx;
    if (temp_vx > 1.0f) temp_vx = 1.0f;
    if (temp_vx < -1.0f) temp_vx = -1.0f;
    Angle->pit = (float)asin(-temp_vx) * RadtoDeg;          /* 俯仰角 (Pitch) */
    Angle->rol = (float)my_atan2(vy, vz) * RadtoDeg;        /* 横滚角 (Roll) */

    /* 偏航角采用积分方式，防止万向节死锁下的跳变 */
    if ((Gyr_rad->Z * RadtoDeg > 1.0f) || (Gyr_rad->Z * RadtoDeg < -1.0f))
    {
        /* 这里的 0.01f 为实际调用 IMUupdate 的采样周期 (10ms) */
        Angle->yaw += Gyr_rad->Z * RadtoDeg * 0.01f;
    }
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
