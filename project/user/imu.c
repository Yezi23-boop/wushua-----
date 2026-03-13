#include "zf_common_headfile.h"
#include "math.h"
float gyro_z = 0;
// 姿态融合 PI 参数（Mahony/Madgwick 思想的简化实现）
// Kp：比例增益，越大对加速度（重力方向）校正越强，收敛快但易抖
// Ki：积分增益，用于补偿陀螺零偏；过大易积累误差，过小收敛慢
#define Kp 5.0f
#define Ki 0.007f

// halfT：半采样周期（即 0.5 * Ts）。若你的传感器更新周期为 5ms，则 halfT = 0.0025f
#define halfT 0.005f

// 部分工具函数需要圆周率，若 math.h 未定义 M_PI 则手动给出
#define M_PI 3.1415926535

// 全局姿态角与传感器滤波输出（单位：角度为度，角速度/加速度见下）
// 结构体 FLOAT_ANGLE、FLOAT_XYZ 由库中头文件定义
FLOAT_ANGLE Att_Angle; // roll/pitch/yaw（单位：度）
FLOAT_XYZ Acc_filt;    // 滤波后的加速度
FLOAT_XYZ Gyr_filt;    // 滤波后的角速度（单位：弧度每秒）
// 说明：有些平台不提供 atan2，这里自写一个等价函数
// 输入 (y, x)，返回区分象限的反正切角（单位：弧度）
double my_atan2(double y, double x)
{
    // 处理 x == 0 的退化情况，避免除零
    if (x == 0)
    {
        if (y > 0)
            return M_PI / 2; // +90°
        else if (y < 0)
            return -M_PI / 2; // -90°
        else
            return 0; // 原点
    }
    else
    {
        // 计算基础角度
        double theta = atan(y / x);

        // 根据 x 符号修正到正确象限
        if (x > 0)
        {
            // 第一、四象限
            return theta;
        }
        else
        {
            // x < 0：第二、三象限
            if (y >= 0)
                return theta + M_PI;
            else
                return theta - M_PI;
        }
    }
}

// 传感器数据预处理：读取原始 IMU 数据，做单位转换与零偏校正
// 依赖外部 API：imu660ra_get_acc / imu660ra_get_gyro / *_transition()
// 依赖外部常量：Gyro_offset_x/y/z、DegtoRad
LowPassFilter_t Gyr_filt_lowpass;
void Prepare_Data(void)
{
	if(imu_flat_star==1)
	{
    // 读取一次原始数据（驱动内部会更新 imu660ra_* 全局变量）
    imu660ra_get_acc();
    imu660ra_get_gyro();

    // 陀螺（单位：弧度每秒）= (原始度每秒 - 零偏) × 度转弧度
    Gyr_filt.X = (imu660ra_gyro_transition(imu660ra_gyro_x) - Gyro_offset_x) * DegtoRad;
    Gyr_filt.Y = (imu660ra_gyro_transition(imu660ra_gyro_y) - Gyro_offset_y) * DegtoRad;
    Gyr_filt.Z = (imu660ra_gyro_transition(imu660ra_gyro_z) - Gyro_offset_z) * DegtoRad;
    gyro_z = imu660ra_gyro_transition(imu660ra_gyro_z) - Gyro_offset_z;
    low_pass_filter_mt(&Gyr_filt_lowpass, &gyro_z, 0.9);
    // 加速度（单位：按驱动转换结果，通常为 g 或 m/s^2）
    Acc_filt.X = imu660ra_acc_transition(imu660ra_acc_x);
    Acc_filt.Y = imu660ra_acc_transition(imu660ra_acc_y);
    Acc_filt.Z = imu660ra_acc_transition(imu660ra_acc_z);
	}
}

// 四元数（单位四元数，用于描述机体姿态，q0 为标量部）
float q0 = 1, q1 = 0, q2 = 0, q3 = 0;

// 中间量（方向余弦估计、误差项、模长等）
float vx, vy, vz, ex, ey, ez, norm;

// 快速 1/sqrt(x) 前置声明（位运算近似）
static float invSqrt(float x);

// 核心姿态更新：基于陀螺积分 + 加速度方向反馈
// Gyr_rad：角速度（弧度/秒）
// Acc_filt：加速度（方向用于估计重力）
// Att_Angle：输出欧拉角（度）
void IMUupdate(FLOAT_XYZ *Gyr_rad, FLOAT_XYZ *Acc_filt, FLOAT_ANGLE *Att_Angle)
{
    // 读取输入（为减少解引用次数）
    float ax = Acc_filt->X, ay = Acc_filt->Y, az = Acc_filt->Z;
    float gx = Gyr_rad->X, gy = Gyr_rad->Y, gz = Gyr_rad->Z;

    // 陀螺零偏的积分估计项（PI 控制中的 I）
    static float exInt = 0, eyInt = 0, ezInt = 0;

    // 保存旧四元数（欧拉步进）
    float q0_old = 0, q1_old = 0, q2_old = 0, q3_old = 0;

    // 1) 将加速度归一化，作为重力方向观测
    norm = invSqrt(ax * ax + ay * ay + az * az); // 1/sqrt(a·a)
    ax *= norm;
    ay *= norm;
    az *= norm;

    // 2) 根据当前四元数估计机体坐标系下的重力方向 v = R*[0,0,1]
    // 公式推导自方向余弦矩阵与四元数的关系
    vx = 2 * (q1 * q3 - q0 * q2);
    vy = 2 * (q0 * q1 + q2 * q3);
    vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

    // 3) 计算重力方向误差 e = a × v
    //   当机体静止或匀速运动时，a ≈ 重力方向；叉积得到方向误差
    ex = (ay * vz - az * vy);
    ey = (az * vx - ax * vz);
    ez = (ax * vy - ay * vx);

    // 4) 误差积分（I 项），用于补偿陀螺零偏
    exInt += ex * Ki;
    eyInt += ey * Ki;
    ezInt += ez * Ki;

    // 5) 将比例 + 积分误差反馈到陀螺，形成 PI 闭环
    gx += Kp * ex + exInt;
    gy += Kp * ey + eyInt;
    gz += Kp * ez + ezInt;

    // 6) 四元数微分积分（离散欧拉积分，采样周期 Ts=2*halfT）
    q0_old = q0;
    q1_old = q1;
    q2_old = q2;
    q3_old = q3;

    // q_dot = 0.5 * q ? [0, gx, gy, gz]
    // 这里用 halfT 已包含 0.5*Ts 的因子，因此直接累加
    q0 += (-q1_old * gx - q2_old * gy - q3_old * gz) * halfT;
    q1 += (q0_old * gx + q2_old * gz - q3_old * gy) * halfT;
    q2 += (q0_old * gy - q1_old * gz + q3_old * gx) * halfT;
    q3 += (q0_old * gz + q1_old * gy - q2_old * gx) * halfT;

    // 7) 归一化四元数，抑制数值漂移
    norm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= norm;
    q1 *= norm;
    q2 *= norm;
    q3 *= norm;

    // 8) 四元数转欧拉角（Z-Y-X，航向-俯仰-横滚）
    // 角度转换为“度”
    //    Att_Angle->rol = (float)(my_atan2(2 * (q0 * q1 + q2 * q3), 1 - 2 * (q1 * q1 + q2 * q2)) * RadtoDeg);
    //    Att_Angle->pit = (float)(asin(2 * (q0 * q2 - q1 * q3)) * RadtoDeg);
    // 航向（yaw）若无磁力计参与，可直接用陀螺积分近似更新（对漂移较敏感）
    // 这里做了小阈值抑制：角速度过小认为是噪声，不更新 yaw
    if ((Gyr_rad->Z * RadtoDeg > 1.0f) || (Gyr_rad->Z * RadtoDeg < -1.0f))
    {
        // 0.01f ≈ 积分步长（单位：秒）。若你的真实采样周期不是 10ms，请等比例调整
        Att_Angle->yaw += Gyr_rad->Z * RadtoDeg * 0.01f;
    }
}

// 快速求 1/sqrt(x) 的近似算法（Quake fast inverse sqrt）
// 误差很小，速度很快；如需更高精度可再迭代一次牛顿法
static float invSqrt(float x)
{
    float halfx = 0.5f * x;
    float y = x;
    long i = *(long *)&y;             // 将 float 按位解释为整数
    i = 0x5f375a86 - (i >> 1);        // 魔数初始近似
    y = *(float *)&i;                 // 按位解释回 float
    y = y * (1.5f - (halfx * y * y)); // 牛顿迭代一步
    return y;
}

float SquareRootFloat(float number)
{
    long i;
    float x, y;
    const float f = 1.5F;

    x = number * 0.5F;
    y = number;
    i = *(long *)&y;
    i = 0x5f3759df - (i >> 1); // 卡马克
    //   i  = 0x5f375a86 - ( i >> 1 );  //Lomont
    y = *(float *)&i;
    y = y * (f - (x * y * y));
    y = y * (f - (x * y * y));
    return number * y;
}
