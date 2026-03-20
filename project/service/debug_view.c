#include "debug_view.h"
#include "zf_common_headfile.h"

/**
 * @brief VOFA+ 上位机交互服务
 * @details 负责解析来自无线串口的 VOFA 指令并打印调试波形数据
 */
void debug_vofa_service(void)
{
    static char vofa_cmd[64];

    /* 从 FIFO 缓冲区解析 VOFA 协议帧 */
    vofa_parse_from_fifo();

    /* 尝试获取一条完整的指令 */
    if (vofa_get_command(vofa_cmd, 64))
    {
        /* 处理指令（如修改参数、切换状态等） */
        handle_vofa_command(vofa_cmd);
    }

    /*
     * 向串口打印波形数据，VOFA+ 上位机可通过此格式显示实时曲线
     * 格式：数据1,数据2,数据3... \n
     */
    printf("%f,%f,%f,%f\n", test_speed_value, PID.left_speed.speed, PID.right_speed.speed, 0.0f);
}

/**
 * @brief 实时运行数据显示
 * @details 在 IPS 屏幕上显示电感偏差、四路原始值、PID 输出及电池电压
 */
void printf_date(void)
{
    /* 第一列：显示偏差与四路电感归一化值 */
    ips114_show_int32(1 * 24, 18 * 0, Err, 3);
    ips114_show_int32(1 * 24, 18 * 1, ad1, 3);
    ips114_show_int32(1 * 24, 18 * 2, ad2, 3);
    ips114_show_int32(1 * 24, 18 * 3, ad3, 3);
    ips114_show_int32(1 * 24, 18 * 4, ad4, 3);

    /* 第三列：显示 PID 转向输出及左右目标速度 */
    ips114_show_float(3 * 24, 18 * 0, PID.steer.output, 3, 1);
    ips114_show_float(3 * 24, 18 * 1, app.speed.speed_run + PID.steer.output, 3, 1);
    ips114_show_float(3 * 24, 18 * 2, app.speed.speed_run - PID.steer.output, 3, 1);

    /* 第六列：显示实时电池电压 */
    ips114_show_int32(6 * 24, 18 * 1, (int32)dianya, 5);
}

/**
 * @brief 电感原始数据与均值显示
 */
void printf_adc(void)
{
    /* 显示归一化后的电感值 */
    ips114_show_int32(1 * 24, 18 * 0, ad1, 3);
    ips114_show_int32(1 * 24, 18 * 1, ad2, 3);
    ips114_show_int32(1 * 24, 18 * 2, ad3, 3);
    ips114_show_int32(1 * 24, 18 * 3, ad4, 3);

    ips114_show_int32(1 * 24, 18 * 6, Err, 4);

    /* 显示原始采样值 RAW */
    ips114_show_int32(3 * 24, 18 * 0, RAW[0], 4);
    ips114_show_int32(3 * 24, 18 * 1, RAW[1], 4);
    ips114_show_int32(3 * 24, 18 * 2, RAW[2], 4);
    ips114_show_int32(3 * 24, 18 * 3, RAW[3], 4);

    /* 显示滑动平均滤波后的值 MA */
    ips114_show_int32(7 * 24, 18 * 0, MA[0], 4);
    ips114_show_int32(7 * 24, 18 * 1, MA[1], 4);
    ips114_show_int32(7 * 24, 18 * 2, MA[2], 4);
    ips114_show_int32(7 * 24, 18 * 3, MA[3], 4);
}

/**
 * @brief IMU 姿态传感器数据显示
 */
void printf_imu(void)
{
    /* 显示滤波后的角速度（陀螺仪） */
    ips114_show_float(4 * 24, 18 * 0, Gyr_filt.X, 4, 2);
    ips114_show_float(4 * 24, 18 * 1, Gyr_filt.Y, 4, 2);
    ips114_show_float(4 * 24, 18 * 2, Gyr_filt.Z, 4, 2);

    /* 显示滤波后的加速度 */
    ips114_show_float(4 * 24, 18 * 4, Acc_filt.X, 4, 2);
    ips114_show_float(4 * 24, 18 * 5, Acc_filt.Y, 4, 2);
    ips114_show_float(4 * 24, 18 * 6, Acc_filt.Z, 4, 2);

    /* 显示计算出的速度与负压风扇状态 */
    ips114_show_float(7 * 24, 18 * 0, vx, 4, 2);
    ips114_show_float(7 * 24, 18 * 1, vy, 4, 2);
    ips114_show_float(7 * 24, 18 * 2, vz, 4, 2);
    ips114_show_int32(7 * 24, 18 * 4, fuya_date, 4);
    ips114_show_int32(7 * 24, 18 * 5, phase, 4);
}

/**
 * @brief 速度与角度环综合测试显示
 */
void printf_speed_test(void)
{
    ips114_show_float(0, 0, test_angle_value, 6, 1);
    ips114_show_float(0, 15, PID.angle.output, 6, 1);
    ips114_show_float(0, 35, speed_l, 6, 1);
    ips114_show_float(0, 55, speed_r, 6, 1);
    ips114_show_float(0, 75, gyro_z, 6, 2);
    ips114_show_float(0, 95, PID.angle.error, 6, 2);

    /* 串口同步上报测试数据 */
    printf("%f,%f,%f,%f\n", test_angle_value, gyro_z, PID.angle.error, PID.angle.output);
}

/**
 * @brief 按键物理状态测试显示
 * @details 检查各按键引脚（P33-P37）的实时电平，方便排查硬件按键故障
 */
void printf_butten_test(void)
{
    ips114_show_int32(4 * 24, 18 * 0, P33, 1);
    ips114_show_int32(4 * 24, 18 * 1, P34, 1);
    ips114_show_int32(4 * 24, 18 * 2, P35, 1);
    ips114_show_int32(4 * 24, 18 * 4, P36, 1);
    ips114_show_int32(4 * 24, 18 * 5, P37, 1);
    ips114_show_int32(4 * 24, 18 * 6, flat_statr, 2);
}
