#include "zf_common_headfile.h"
#include "eeprom.h"

/* 数据缓冲区，用于与 IAP 接口交换数据 */
uint8 date_buff[200];
static uint8 eeprom_init_time = 0;
AppConfig g_app_config;

/* 内部私有函数声明 */
static void eeprom_load_defaults(AppConfig *config);
static void eeprom_read_config(AppConfig *config);
static void eeprom_write_config(const AppConfig *config);
static void save_int(int32 input, uint8 value_bit);
static int32 read_int(uint8 value_bit);
static void save_float(float input, uint8 value_bit);
static float read_float(uint8 value_bit);

/**
 * @brief 加载系统默认参数
 * @details 当 EEPROM 中无有效数据时，使用此组硬编码参数
 */
static void eeprom_load_defaults(AppConfig *config)
{
    config->start.start_flag = 1;
    config->start.circle_flags = 0;
    config->start.fuya_xili = 2000.00f;

    config->speed.kp_Err = 0.70f;
    config->speed.kd_Err = 0.70f;
    config->speed.speed_run = 30.00f;
    config->speed.limiting_Err = 600.00f;
    config->speed.kp2_Err = 0.01f;

    config->angle.kp_Angle = 0.60f;
    config->angle.kd_Angle = 0.20f;
    config->angle.limiting_Angle = 30.00f;
    config->angle.A_1 = 0.50f;
    config->angle.B_1 = 1.00f;
    config->angle.C_l = 0.60f;

    config->ring.ring_encoder = 15.00f;
    config->ring.pre_ring_Gyro_set = 210.00f;
    config->ring.in_ring_Gyroz = 220.00f;
    config->ring.pre_out_ring_Gyro_set = 170.00f;
    config->ring.pre_out_ring_Gyroz = 350.00f;
    config->ring.pre_out_ring_encoder = 30.00f;

    config->fly.count_fly_speed = 15;
    config->fly.count_fly_time_1 = 3;
    config->fly.count_fly_time_2 = 50;
    config->fly.count_fly_angle = 0;
    config->fly.fly_ramp_enable = 0; /* 默认关闭飞坡检测 */
}

/**
 * @brief 从缓冲区解析配置到结构体
 */
static void eeprom_read_config(AppConfig *config)
{
    config->start.start_flag = (int16)read_int(1);
    config->start.circle_flags = (int16)read_int(2);

    config->speed.kp_Err = read_float(4);
    config->start.fuya_xili = read_float(5);
    config->speed.kd_Err = read_float(6);
    config->speed.kp2_Err = read_float(7);
    config->speed.speed_run = read_float(8);
    config->speed.limiting_Err = read_float(9);

    config->angle.kp_Angle = read_float(10);
    config->angle.kd_Angle = read_float(11);
    config->angle.limiting_Angle = read_float(12);
    config->angle.B_1 = read_float(13);
    config->angle.C_l = read_float(14);
    config->angle.A_1 = read_float(15);

    config->ring.ring_encoder = read_float(16);
    config->ring.pre_ring_Gyro_set = read_float(17);
    config->ring.in_ring_Gyroz = read_float(18);
    config->ring.pre_out_ring_Gyro_set = read_float(19);
    config->ring.pre_out_ring_Gyroz = read_float(20);
    config->ring.pre_out_ring_encoder = read_float(21);

    config->fly.count_fly_speed = (int)read_int(22);
    config->fly.count_fly_time_1 = (int)read_int(23);
    config->fly.count_fly_time_2 = (int)read_int(24);
    config->fly.count_fly_angle = (int)read_int(25);
    config->fly.fly_ramp_enable = (int16)read_int(26);
}

/**
 * @brief 将配置结构体序列化到缓冲区
 */
static void eeprom_write_config(const AppConfig *config)
{
    save_int(config->start.start_flag, 1);
    save_int(config->start.circle_flags, 2);

    save_float(config->speed.kp_Err, 4);
    save_float(config->start.fuya_xili, 5);
    save_float(config->speed.kd_Err, 6);
    save_float(config->speed.kp2_Err, 7);
    save_float(config->speed.speed_run, 8);
    save_float(config->speed.limiting_Err, 9);

    save_float(config->angle.kp_Angle, 10);
    save_float(config->angle.kd_Angle, 11);
    save_float(config->angle.limiting_Angle, 12);
    save_float(config->angle.B_1, 13);
    save_float(config->angle.C_l, 14);
    save_float(config->angle.A_1, 15);

    save_float(config->ring.ring_encoder, 16);
    save_float(config->ring.pre_ring_Gyro_set, 17);
    save_float(config->ring.in_ring_Gyroz, 18);
    save_float(config->ring.pre_out_ring_Gyro_set, 19);
    save_float(config->ring.pre_out_ring_Gyroz, 20);
    save_float(config->ring.pre_out_ring_encoder, 21);

    save_int(config->fly.count_fly_speed, 22);
    save_int(config->fly.count_fly_time_1, 23);
    save_int(config->fly.count_fly_time_2, 24);
    save_int(config->fly.count_fly_angle, 25);
    save_int(config->fly.fly_ramp_enable, 26);
}

/**
 * @brief EEPROM 初始化
 * @details 初始化 IAP 模块并读取历史配置，若首次运行则格式化
 */
void eeprom_init(void)
{
    /* 初始化 IAP (In-Application Programming) 模块 */
    iap_init();
    /* 从扇区 0 读取 200 字节到缓冲区 */
    iap_read_buff(0x00, date_buff, sizeof(date_buff));

    /* 预加载默认值 */
    eeprom_load_defaults(&g_app_config);

    /* 检查索引 0 的标志位，判断是否为有效配置 */
    eeprom_init_time = (uint8)read_int(0);

    if (eeprom_init_time != 1)
    {
        /* 若未初始化，则写入初始化标志并保存默认配置 */
        eeprom_init_time = 1;
        save_int(eeprom_init_time, 0);
        eeprom_flash();
    }
    else
    {
        /* 若已初始化，则加载 EEPROM 中的真实数据 */
        eeprom_read_config(&g_app_config);
    }
}

/**
 * @brief 执行 Flash 刷写操作
 */
void eeprom_flash(void)
{
    eeprom_write_config(&g_app_config);
}

/* --- 底层读写辅助函数 --- */

/**
 * @brief 将 int32 类型数据保存到缓冲区指定位置并同步到 Flash
 * @param input 数据
 * @param value_bit 逻辑索引（每个索引对应 4 字节）
 */
static void save_int(int32 input, uint8 value_bit)
{
    uint8 i;
    uint8 begin = value_bit * 4;
    uint8 *p = (uint8 *)&input;

    for (i = 0; i < 4; i++)
    {
        date_buff[begin++] = *(p + i);
    }
    /* 立即刷写到 Flash 扇区 */
    extern_iap_write_buff(0x00, date_buff, sizeof(date_buff));
}

/**
 * @brief 从缓冲区读取 int32 类型数据
 */
static int32 read_int(uint8 value_bit)
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

/**
 * @brief 将 float 类型数据保存到缓冲区并同步到 Flash
 */
static void save_float(float input, uint8 value_bit)
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

/**
 * @brief 从缓冲区读取 float 类型数据
 */
static float read_float(uint8 value_bit)
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
