/**
 * @file imu.c
 * @brief IMU 姿态辅助计算与角速度桥接
 * @details
 * 本模块对 IMU660RC 输出进行轻量转换，向控制环提供：
 * - 基于四元数的重力向量 Z 分量缓存；
 * - 统一量纲后的 gyro_z 实时反馈；
 * - 若干数学辅助函数（快速平方根、反平方根、atan2 兼容实现）。
 *
 * 设计原则：尽量减少高频路径计算开销，同时保证姿态量的可解释性。
 */
#include "zf_common_headfile.h"
#include "math.h"
#include "imu.h"

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

/**< Z 轴陀螺仪向外输出前缩放乘数：由底盘转向几何、硬件灵敏度及控制目标共同决定的经验值 */
#define IMU_GYRO_Z_SCALE (0.005f)
#define IMU_GYRO_Z_SIGN (-1.0f) /* 驱动 gyro_z 顺时针为正；控制差速约定左转为正，需在桥接层翻转。 */
#define IMU_GYRO_ZERO_CALIB_SAMPLES (64)
#define IMU_GYRO_ZERO_CALIB_DELAY_MS (4)
#define IMU_GRAVITY_Z_SIGN (-1.0f) /* 当前 IMU 安装方向下，驱动解算平地 vz 为 -1；统一翻转为上层平地约 +1。 */
#define IMU_GRAVITY_VZ_FLAT_COMP_START 0.80f /* 仅在接近平地时补偿；低于该值认为可能是真实姿态变化。 */
#define IMU_GRAVITY_VZ_COMP_STEP 0.0015f     /* 5ms 每次最多补 0.0015，约 0.3/s，避免机械抖动导致平地 vz 慢慢掉。 */
#define IMU_GRAVITY_VZ_COMP_MAX 0.20f        /* 最多补 0.20，防止补偿掩盖圆桶或墙面姿态。 */

volatile float gyro_z = 0.0f;
static float imu_gyro_z_zero_bias = 0.0f;
static volatile float imu_gravity_vz = 1.0f;
static float imu_gravity_vz_time_comp = 0.0f;

/**
 * @brief 上电标定 gyro_z 零偏
 * @details
 * 在车辆静止状态下连续采样，计算均值作为零偏。
 * 标定只应在初始化阶段调用，避免运动中误采样。
 */
void imu_calibrate_gyro_z_zero_drift(void)
{
    uint16 i;
    int32 gyro_raw_sum;

    gyro_raw_sum = 0;

    /* 预留一个短暂稳定窗口，等待 IMU 输出稳定。 */
    system_delay_ms(20);

    for (i = 0; i < IMU_GYRO_ZERO_CALIB_SAMPLES; i++)
    {
        gyro_raw_sum += (int32)imu660rc_gyro_z;
        system_delay_ms(IMU_GYRO_ZERO_CALIB_DELAY_MS);
    }

    if (imu660rc_transition_factor[1] > 0.001f)
    {
        imu_gyro_z_zero_bias =
            ((float)gyro_raw_sum / (float)IMU_GYRO_ZERO_CALIB_SAMPLES) /
            imu660rc_transition_factor[1];
    }
    else
    {
        imu_gyro_z_zero_bias = 0.0f;
    }
    gyro_z = 0.0f;
}

/**
 * @brief 由四元数更新重力向量 Z 分量缓存。
 * @details
 * 驱动层四元数顺序为 [y, x, z, w]，此处先重排为标准 (w,x,y,z) 再计算。
 * 当前硬件安装方向下，驱动解算出的 Z 分量与控制策略约定相反，
 * 因此统一在此处翻转 Z 轴，保证负压、圆桶和调试显示共用“平地 vz 约 +1”的坐标系。
 *
 * @note 由 5ms 主控制链路调用一次，其他模块通过 `imu_get_gravity_vz` 读取缓存，避免重复计算四元数。
 */
void imu_update_gravity_vz_from_quaternion(void)
{
    float qw;
    float qx;
    float qy;
    float qz;
    float comp_need;
    float raw_vz;
    float vz;

    qx = imu660rc_quarternion[1];
    qy = imu660rc_quarternion[0];
    qz = imu660rc_quarternion[2];
    qw = imu660rc_quarternion[3];

    raw_vz = IMU_GRAVITY_Z_SIGN * (qw * qw - qx * qx - qy * qy + qz * qz);
    if (raw_vz > 1.0f)
    {
        raw_vz = 1.0f;
    }
    else if (raw_vz < -1.0f)
    {
        raw_vz = -1.0f;
    }

    if (raw_vz > IMU_GRAVITY_VZ_FLAT_COMP_START && raw_vz < 1.0f)
    {
        comp_need = 1.0f - raw_vz;
        if (comp_need > IMU_GRAVITY_VZ_COMP_MAX)
        {
            comp_need = IMU_GRAVITY_VZ_COMP_MAX;
        }

        imu_gravity_vz_time_comp += IMU_GRAVITY_VZ_COMP_STEP;
        if (imu_gravity_vz_time_comp > comp_need)
        {
            imu_gravity_vz_time_comp = comp_need;
        }
    }
    else
    {
        // 离开接近平地区间时立即清补偿，避免真实圆桶/墙面姿态被持续抬高。
        imu_gravity_vz_time_comp = 0.0f;
    }

    vz = raw_vz + imu_gravity_vz_time_comp;
    if (vz > 1.0f)
    {
        vz = 1.0f;
    }

    imu_gravity_vz = vz;
}

/**
 * @brief 读取最近一次 5ms 更新的重力向量 Z 分量。
 * @return float 已限幅到 -1.0f~1.0f 的 vz，平地约 +1。
 */
float imu_get_gravity_vz(void)
{
    return imu_gravity_vz;
}

/**
 * @brief 更新控制环使用的 Z 轴角速度
 * @details 将驱动原始值转换为控制器统一量纲，并统一为左转/逆时针正，避免控制环分散处理符号。
 */
void imu_update_gyro_z_from_imu660rc(void)
{
    float gyro_z_now;

    /* 统一在此处做量纲转换，其他模块直接读 gyro_z */
    gyro_z_now = imu660rc_gyro_transition(imu660rc_gyro_z);
    gyro_z = (gyro_z_now - imu_gyro_z_zero_bias) * IMU_GYRO_Z_SCALE * IMU_GYRO_Z_SIGN;
}

/**
 * @brief C89 环境下的 atan2 兼容实现
 * @details 用于需要明确象限判定的几何计算路径。
 */
double my_atan2(double y, double x)
{
    /* x==0 时单独处理，避免除零并保持象限正确 */
    if (x == 0.0)
    {
        if (y > 0.0)
        {
            return M_PI / 2.0;
        }
        if (y < 0.0)
        {
            return -M_PI / 2.0;
        }
        return 0.0;
    }

    if (x > 0.0)
    {
        /* 第一/第四象限 */
        return atan(y / x);
    }

    if (y >= 0.0)
    {
        /* 第二象限 */
        return atan(y / x) + M_PI;
    }
    /* 第三象限 */
    return atan(y / x) - M_PI;
}

/**
 * @brief 快速反平方根近似
 * @details
 * 使用经典位级近似 + 一次牛顿迭代，适合对速度敏感且可容忍小误差的场景。
 * 避开了标准库 sqrt 的多次循环开销，能够将计算周期控制在极短时间内。
 */
float invSqrt(float x)
{
    float halfx;
    float y;
    long i;

    /* 1) 位级初值估计 */
    halfx = 0.5f * x;
    y = x;
    i = *(long *)&y;
    i = 0x5f375a86 - (i >> 1);
    y = *(float *)&i;
    /* 2) 一次牛顿迭代提升精度 */
    y = y * (1.5f - (halfx * y * y));
    return y;
}

/**
 * @brief 快速平方根近似
 * @details 通过反平方根近似得到 sqrt(number)，用于高频路径的快速估算。
 */
float SquareRootFloat(float number)
{
    long i;
    float x;
    float y;

    /* 通过反平方根近似得到 sqrt(number) = number * rsqrt(number) */
    x = number * 0.5f;
    y = number;
    i = *(long *)&y;
    i = 0x5f3759df - (i >> 1);
    y = *(float *)&i;
    y = y * (1.5f - (x * y * y));
    return number * y;
}
