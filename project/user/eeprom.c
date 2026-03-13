#include "zf_common_headfile.h"

/*********************************************
 * EEPROM系统基础变量定义
 *********************************************/
uint8 date_buff[200]; // eeprom数据数组
uint8 eeprom_init_time = 0;

/*********************************************
 * 需要被修改的参数
 *********************************************/
// 启动配置参数
int16 start_flag = 1;
int16 circle_flags = 0;
// PID速度控制参数
float kp_Err = 0.70f;
float kd_Err = 0.70f;
float speed_run = 30.00f;
float limiting_Err = 600.00f;
float fuya_xili = 2000.00f;
float kp2_Err = 0.01f;

// PID角度控制参数
float kp_Angle = 0.60f;
float kd_Angle = 0.20f;
float limiting_Angle = 30.00f;
float A_1 = 0.50f;
float B_1 = 1.00f;
float C_l = 0.60f;

// 圆环控制参数
float ring_encoder = 15.00f;
float pre_ring_Gyro_set = 210.00f;
float in_ring_Gyroz = 220.00f;
float pre_out_ring_Gyro_set = 170.00f;
float pre_out_ring_Gyroz = 350.00f;
float pre_out_ring_encoder = 30.00f;
int count_fly_speed = 15;
int count_fly_time_1 = 3;
int count_fly_time_2 = 50;
int count_fly_angle = 0;
/*********************************************
 * EEPROM初始化函数
 *********************************************/
void eeprom_init()
{
    iap_init(); // 初始化EEPROM

    iap_read_buff(0x00, date_buff, sizeof(date_buff)); // 从EEPROM中读取数据

    eeprom_init_time = read_int(0); // eeprom没有被填充，则会读到垃圾值

    if (eeprom_init_time != 1) // 初次启动，eeprom_init_time为垃圾值，if成立
    {
        eeprom_init_time = 1;
        save_int(eeprom_init_time, 0); // 填充eeprom_init_time的值到eeprom

        eeprom_flash(); // 填充源码变量初始化的值到eeprom
    }
    else // 非初次启动，读取eeprom用于赋值变量
    {
        // 启动配置参数
        start_flag = read_int(1);
        circle_flags = read_int(2);
        // PID速度控制参数
        kp_Err = read_float(4);
        fuya_xili = read_float(5);
        kd_Err = read_float(6);
        kp2_Err = read_float(7);
        speed_run = read_float(8);
        limiting_Err = read_float(9);

        // PID角度控制参数
        kp_Angle = read_float(10);
        kd_Angle = read_float(11);
        limiting_Angle = read_float(12);
        B_1 = read_float(13);
        C_l = read_float(14);
        A_1 = read_float(15);

        // ??????????
        ring_encoder = read_float(16);
        pre_ring_Gyro_set = read_float(17);
        in_ring_Gyroz = read_float(18);
        pre_out_ring_Gyro_set = read_float(19);
        pre_out_ring_Gyroz = read_float(20);
        pre_out_ring_encoder = read_float(21);
        count_fly_speed = read_int(22);
        count_fly_time_1 = read_int(23);
        count_fly_time_2 = read_int(24);
        count_fly_angle = read_int(25);
    }
}

/*********************************************
 * 刷写保存数据到eeprom
 *********************************************/
void eeprom_flash()
{
    // 启动配置参数
    save_int(start_flag, 1);
    save_int(circle_flags, 2);

    // PID速度控制参数
    save_float(kp_Err, 4);
    save_float(fuya_xili, 5);
    save_float(kd_Err, 6);
    save_float(kp2_Err, 7);
    save_float(speed_run, 8);
    save_float(limiting_Err, 9);

    // PID角度控制参数
    save_float(kp_Angle, 10);
    save_float(kd_Angle, 11);
    save_float(limiting_Angle, 12);
    save_float(B_1, 13);
    save_float(C_l, 14);
    save_float(A_1, 15);

    // 圆环控制参数
    save_float(ring_encoder, 16);
    save_float(pre_ring_Gyro_set, 17);
    save_float(in_ring_Gyroz, 18);
    save_float(pre_out_ring_Gyro_set, 19);
    save_float(pre_out_ring_Gyroz, 20);
    save_float(pre_out_ring_encoder, 21);
    save_int(count_fly_speed, 22);
    save_int(count_fly_time_1, 23);
    save_int(count_fly_time_2, 24);
    save_int(count_fly_angle, 25);
}

/*********************************************
 * EEPROM底层读写函数
 *********************************************/
void save_int(int32 input, uint8 value_bit)
{
    uint8 i;
    uint8 begin = value_bit * 4;
    uint8 *p = (uint8 *)&input;

    for (i = 0; i < 4; i++)
    {
        date_buff[begin++] = *(p + i);
    }
    extern_iap_write_buff(0x00, date_buff, sizeof(date_buff));
}

int32 read_int(uint8 value_bit)
{
    uint8 i;
    uint8 begin = value_bit * 4;
    int32 output;
    uint8 *p = (uint8 *)&output;

    for (i = 0; i < 4; i++)
    {
        *(p + i) = date_buff[begin++];
    }
    return output;
}

void save_float(float input, uint8 value_bit)
{
    uint8 i;
    uint8 begin = value_bit * 4;
    uint8 *p = (uint8 *)&input;

    for (i = 0; i < 4; i++)
    {
        date_buff[begin++] = *(p + i);
    }
    extern_iap_write_buff(0x00, date_buff, sizeof(date_buff));
}

float read_float(uint8 value_bit)
{
    uint8 i;
    uint8 begin = value_bit * 4;
    float output;
    uint8 *p = (uint8 *)&output;

    for (i = 0; i < 4; i++)
    {
        *(p + i) = date_buff[begin++];
    }
    return output;
}
