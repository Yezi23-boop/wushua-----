/**
 * @file debug_view.c
 * @brief 调试视图与屏幕输出统筹
 * @details
 * 负责在 IPS 显示屏上提供多组数据监控页面（电感、外设、IMU、控制输出等）。
 *
 * 资源提醒：
 * 刷屏函数具有可观的延迟开销（约几毫秒量级），本模块只能在主循环空闲期间被调用，
 * 严禁放置于任何硬件中断（TM0/TM1）中，否则将引发严重的高频控制环丢帧。
 */
#include "zf_common_headfile.h"

#define DEBUG_VIEW_ENABLE_SPEED_LOOP_AUTOTUNE 0

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
 * @brief IMU 姿态传感器数据显示
 */
void printf_imu(void)
{
    ips114_show_float(4 * 24, 18 * 0, imu660rc_roll, 4, 1);
    ips114_show_float(4 * 24, 18 * 1, imu660rc_pitch, 4, 1);
    ips114_show_float(4 * 24, 18 * 2, imu660rc_yaw, 4, 1);
    ips114_show_float(4 * 24, 18 * 3, imu660rc_quarternion[0], 1, 3);
    ips114_show_float(4 * 24, 18 * 4, imu660rc_quarternion[1], 1, 3);
    ips114_show_float(4 * 24, 18 * 5, imu660rc_quarternion[2], 1, 3);
    ips114_show_float(4 * 24, 18 * 6, imu660rc_quarternion[3], 1, 3);

    ips114_show_float(0, 18 * 0, imu660rc_gyro_x, 6, 1);
    ips114_show_float(0, 18 * 1, imu660rc_gyro_y, 6, 1);
    ips114_show_float(0, 18 * 2, imu660rc_gyro_z, 6, 1);
    ips114_show_float(0, 18 * 3, gyro_z, 4, 1);
    ips114_show_float(0, 18 * 5, imu660rc_acc_y, 6, 1);
    ips114_show_float(0, 18 * 6, imu660rc_acc_z, 6, 1);
    /* 预留一行可按需显示更多 gyro 调试量 */
}

/**
 * @brief 速度与直接差速综合测试显示
 */
void printf_speed_test(void)
{
    ips114_show_float(0, 0, test_diff_value, 6, 1);
    ips114_show_float(0, 15, PID.left_speed.output, 6, 1);
    ips114_show_float(0, 35, speed_l, 6, 1);
    ips114_show_float(0, 55, speed_r, 6, 1);
    ips114_show_float(0, 75, gyro_z, 6, 2);
    ips114_show_float(0, 95, PID.right_speed.output, 6, 1);

    /* 串口同步上报测试数据 */
    printf("%f,%f,%f,%f\n", test_diff_value, gyro_z, speed_l, speed_r);
}

/**
 * @brief 按键与 IMU 中断脚状态测试显示
 * @details 四个按键使用 P26/P41/P40/P37，P34/P35 保留给 TPL0102 软件 I2C。
 */
void printf_butten_test(void)
{
    ips114_show_string(0, 18 * 0, "K1 P26");
    ips114_show_int32(7 * 24, 18 * 0, P26, 1);

    ips114_show_string(0, 18 * 1, "K2 P41");
    ips114_show_int32(7 * 24, 18 * 1, P41, 1);

    ips114_show_string(0, 18 * 2, "K3 P40");
    ips114_show_int32(7 * 24, 18 * 2, P40, 1);

    ips114_show_string(0, 18 * 3, "K4 P37");
    ips114_show_int32(7 * 24, 18 * 3, P37, 1);

    ips114_show_string(0, 18 * 5, "INT P33");
    ips114_show_int32(7 * 24, 18 * 5, P33, 1);

    ips114_show_string(0, 18 * 6, "FLAG");
    ips114_show_int32(7 * 24, 18 * 6, a_run_mode_get_start_state(), 2);
}
