#include "debug_view.h"
#include "vofa.h"
#include "zf_common_headfile.h"

#define DEBUG_VIEW_ENABLE_SPEED_LOOP_AUTOTUNE 0
#define IMU_UART_PRINT_DIV 12

/**
 * @brief VOFA+ 上位机交互服务
 * @details 负责解析来自无线串口的 VOFA 指令并打印调试波形数据
 */
void debug_vofa_service(void)
{
#if DEBUG_VIEW_ENABLE_SPEED_LOOP_AUTOTUNE
    vofa_service();
#else
    vofa_service_legacy();
#endif
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
    uint8 updated = 0;
    static uint8 imu_uart_print_count = 0;

    updated = imu660rc_service();

    if (updated)
    {
        imu_uart_print_count++;
        if (IMU_UART_PRINT_DIV <= imu_uart_print_count)
        {
            imu_uart_print_count = 0;
            ips114_show_float(4 * 24, 18 * 0, imu660rc_roll, 4, 1);
            ips114_show_float(4 * 24, 18 * 1, imu660rc_pitch, 4, 1);
            ips114_show_float(4 * 24, 18 * 2, imu660rc_yaw, 4, 1);
            ips114_show_float(4 * 24, 18 * 3, imu660rc_quarternion[0], 1, 3);
            ips114_show_float(4 * 24, 18 * 4, imu660rc_quarternion[1], 1, 3);
            ips114_show_float(4 * 24, 18 * 5, imu660rc_quarternion[2], 1, 3);
            ips114_show_float(4 * 24, 18 * 6, imu660rc_quarternion[3], 1, 3);
        }
    }
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
 * @brief 按键与 IMU 中断脚状态测试显示
 * @details 四个按键改到 P26/P35/P34/P37，P33 单独作为 IMU660RC 的 INT1 观察
 */
void printf_butten_test(void)
{
    ips114_show_string(0, 18 * 0, "K1 P26");
    ips114_show_int32(7 * 24, 18 * 0, P26, 1);

    ips114_show_string(0, 18 * 1, "K2 P35");
    ips114_show_int32(7 * 24, 18 * 1, P35, 1);

    ips114_show_string(0, 18 * 2, "K3 P34");
    ips114_show_int32(7 * 24, 18 * 2, P34, 1);

    ips114_show_string(0, 18 * 3, "K4 P37");
    ips114_show_int32(7 * 24, 18 * 3, P37, 1);

    ips114_show_string(0, 18 * 5, "INT P33");
    ips114_show_int32(7 * 24, 18 * 5, P33, 1);

    ips114_show_string(0, 18 * 6, "FLAG");
    ips114_show_int32(7 * 24, 18 * 6, flat_statr, 2);
}
