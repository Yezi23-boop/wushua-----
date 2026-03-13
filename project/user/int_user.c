#include "zf_common_headfile.h"
#define TIME_0 5  // 定时器0中断周期(ms)
#define TIME_1 10 // 定时器1中断周期(ms)
void int_user(void)
{
    // 系统初始化：传感器/存储/定时器/编码器/ADC/电机/无线
    system_delay_init(); // 初始化延时模块
    ips114_init();
    imu660ra_init(); // 初始化 IMU660RA
    eeprom_init();
    pit_ms_init(TIM0_PIT, TIME_0);
    pit_ms_init(TIM1_PIT, TIME_1);
    encoder_dir_init(TIM3_ENCOEDER, IO_P46, TIM3_ENCOEDER_P04); // 编码器初始化
    encoder_dir_init(TIM4_ENCOEDER, IO_P42, TIM4_ENCOEDER_P06); // 编码器初始化
    adc_init(ADC_CH13_P05, ADC_8BIT);
    adc_init(ADC_CH0_P10, ADC_12BIT);
    adc_init(ADC_CH1_P11, ADC_12BIT);
    adc_init(ADC_CH8_P00, ADC_12BIT);
    adc_init(ADC_CH9_P01, ADC_12BIT);
    motor_Init();         // 电机初始化
    fuya_Init();          // 负压系统初始化
    wireless_uart_init(); // 无线串口初始化
	offset_init();
    // 速度环 PID 初始化（误差限幅与输出限幅）
    pid_speed_init(&PID.left_speed, 120, 50, 0, 9000, 9000);
    pid_speed_init(&PID.right_speed, 120, 50, 0, 9000, 9000);
    // 方向环 PID 初始化（误差KP/KD与陀螺KD分离）
    pid_steer_init(&PID.steer, kp_Err, kd_Err, kp2_Err, limiting_Err, limiting_Err);   // 方向环
    pid_steer_init(&PID.angle, kp_Angle, kd_Angle, 0, limiting_Angle, limiting_Angle); // 角度环
}

/*********************************************
 * 传感器零偏标定
 * 函数: offset_init()
 * 作用: 采样多次取平均，得到陀螺仪与加速度零偏
 * 日期: 2024/11/26
 * 备注: 调用前确保 IMU 已上电稳定
 *********************************************/
float Gyro_offset_x = 0;
float Gyro_offset_y = 0;
float Gyro_offset_z = 0;
float acc_offset_x = 0;
float acc_offset_y = 0;
float acc_offset_z = 0;
int imu_flat_star=0;
void offset_init(void)
{
    int rt = 50;
    int i;
    for (i = 0; i < rt; i++)
    {
        imu660ra_get_gyro();
        imu660ra_get_acc();
        Gyro_offset_x += imu660ra_gyro_transition(imu660ra_gyro_x);
        Gyro_offset_y += imu660ra_gyro_transition(imu660ra_gyro_y);
        Gyro_offset_z += imu660ra_gyro_transition(imu660ra_gyro_z);
        acc_offset_x += (imu660ra_acc_transition(imu660ra_acc_x));
        acc_offset_y += (imu660ra_acc_transition(imu660ra_acc_y));
        acc_offset_z += (imu660ra_acc_transition(imu660ra_acc_z));
        system_delay_ms(5);
    }
    Gyro_offset_x = Gyro_offset_x / rt;
    Gyro_offset_y = Gyro_offset_y / rt;
    Gyro_offset_z = Gyro_offset_z / rt;
    acc_offset_x = acc_offset_x / rt;
    acc_offset_y = acc_offset_y / rt;
    acc_offset_z = acc_offset_z / rt;
	imu_flat_star=1;
}
