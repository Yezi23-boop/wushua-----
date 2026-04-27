#include "zf_common_headfile.h"

/* 数据缓冲区，用于与 IAP 接口交换数据，大小为 200 字节 */
uint8 date_buff[200];
/* EEPROM 初始化标志位，用于判断是否为首次上电（0-首次，1-非首次） */
static uint8 eeprom_init_time = 0;
/* 全局配置结构体实例，运行时所有的参数都从这里读取 */
AppConfig app;

/* 内部私有函数声明 */
static void eeprom_load_defaults(AppConfig *config);
static void eeprom_read_config(AppConfig *config);
static void eeprom_write_config(const AppConfig *config);
static void save_int(int32 input, uint8 value_bit);
static int32 read_int(uint8 value_bit);
static void save_float(float input, uint8 value_bit);
static float read_float(uint8 value_bit);
static float clamp_percent_value(float value, float default_value);

/**
 * @brief 加载系统默认参数
 * @details 当检测到 EEPROM 中无有效数据（首次运行）时，使用此函数将硬编码的默认参数填充到 config 结构体中。
 * 这些参数经过预先调试，能保证小车基本的稳定运行。
 * @param config 指向需要填充默认值的配置结构体指针
 */
static void eeprom_load_defaults(AppConfig *config)
{
    /* 启动与基础配置默认值 */
    config->start.start_flag = 1;             /* 默认启动 */
    config->start.circle_flags = 0;           /* 默认自动识别圆环方向 */
    config->start.fuya_xili = 50.00f;         /* 默认平地负压百分比 */
    config->start.fuya_wall_percent = 60.00f; /* 默认墙面负压百分比 */

    /* 速度环 PID 默认参数 */
    config->speed.kp_Err = 4.50f;  // 4.50
    config->speed.kd_Err = 10.00f; // 4.50
    config->speed.gyro_damp_Err = 0.00f;
    config->speed.speed_run = 35.00f; /* 默认基础速度 30 */ // 4.50
    config->speed.limiting_Err = 600.00f; /* 转向限幅 */    // 4.50
    config->speed.kp2_Err = 0.00f;

    /* 电感偏差解算默认参数 */
    config->angle.kp_Angle = 0.80f;
    config->angle.kd_Angle = 0.20f;
    config->angle.gyro_feedback_scale = 1.00f;
    config->angle.limiting_Angle = 35.00f;
    config->angle.A_1 = 0.80f;
    config->angle.B_1 = 1.20f;
    config->angle.C_l = 0.60f;

    /* 圆环策略默认参数 */
    config->ring.ring_encoder = 15.00f;           /* 入环积分阈值 */
    config->ring.pre_ring_Gyro_set = 210.00f;     /* 入环直接差速力度 */
    config->ring.in_ring_Gyroz = 220.00f;         /* 环内角速度 */
    config->ring.pre_out_ring_Gyro_set = 170.00f; /* 出环直接差速力度 */
    config->ring.pre_out_ring_Gyroz = 350.00f;    /* 出环角速度阈值 */
    config->ring.pre_out_ring_encoder = 30.00f;   /* 出环积分阈值 */

    /* 飞坡策略默认参数 */
    config->fly.count_fly_speed = 15;   /* 飞坡慢速值 */
    config->fly.count_fly_time_1 = 6;   /* 触发检测次数 (6 * 5ms = 30ms) */
    config->fly.count_fly_time_2 = 100; /* 状态保持时间 (100 * 5ms = 500ms) */
    config->fly.count_fly_angle = 0;    /* 舵机锁死角度 */
    config->fly.fly_ramp_enable = 1;    /* 默认关闭飞坡检测，防止误触发 */
}

/**
 * @brief 从 EEPROM 缓冲区解析配置数据到结构体
 * @details 按照固定的索引顺序（value_bit），将 date_buff 中的二进制数据还原为结构体成员变量。
 * @param config 指向接收数据的配置结构体指针
 */
static void eeprom_read_config(AppConfig *config)
{
    uint8 percent_migrated;

    config->start.start_flag = (int16)read_int(1);
    config->start.circle_flags = (int16)read_int(2);

    config->speed.kp_Err = read_float(4);
    config->start.fuya_xili = read_float(5);
    config->speed.kd_Err = read_float(6);
    config->speed.kp2_Err = read_float(7);
    config->speed.speed_run = read_float(8);
    config->speed.limiting_Err = read_float(9);
    config->speed.gyro_damp_Err = read_float(10);

    /* 槽位 3 复用为角速度内环限幅；旧版本可能为未初始化值，需做范围回退 */
    config->angle.limiting_Angle = read_float(3);

    /* 11/12 映射为角速度内环参数，保留原 EEPROM 布局以兼容旧数据 */
    config->angle.kp_Angle = read_float(11);
    config->angle.kd_Angle = read_float(12);
    config->angle.B_1 = read_float(13);
    config->angle.C_l = read_float(14);
    config->angle.A_1 = read_float(15);
    config->angle.gyro_feedback_scale = read_float(28);

    if (config->angle.gyro_feedback_scale < 1.0f ||
        config->angle.gyro_feedback_scale > 50.0f)
    {
        config->angle.gyro_feedback_scale = 10.0f;
    }

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
    config->start.fuya_wall_percent = read_float(27);

    percent_migrated = 0;
    if (config->start.fuya_xili < 0.0f || config->start.fuya_xili > 100.0f)
    {
        config->start.fuya_xili = 20.0f;
        percent_migrated = 1;
    }

    if (config->start.fuya_wall_percent < 0.0f || config->start.fuya_wall_percent > 100.0f)
    {
        config->start.fuya_wall_percent = 70.0f;
    }
    else if (percent_migrated && config->start.fuya_wall_percent == 0.0f)
    {
        config->start.fuya_wall_percent = 70.0f;
    }

    config->start.fuya_xili = clamp_percent_value(config->start.fuya_xili, 20.0f);
    config->start.fuya_wall_percent = clamp_percent_value(config->start.fuya_wall_percent, 70.0f);
}

/**
 * @brief 将配置结构体序列化并写入 Flash
 * @details 按照固定的索引顺序（value_bit），将结构体成员变量转换为二进制并写入 Flash。
 * 注意：每次调用 save_xxx 函数都会触发一次 Flash 写入操作。
 * @param config 指向源数据的配置结构体指针
 */
static void eeprom_write_config(const AppConfig *config)
{
    save_int(config->start.start_flag, 1);
    save_int(config->start.circle_flags, 2);
    save_float(config->angle.limiting_Angle, 3);

    save_float(config->speed.kp_Err, 4);
    save_float(clamp_percent_value(config->start.fuya_xili, 20.0f), 5);
    save_float(config->speed.kd_Err, 6);
    save_float(config->speed.kp2_Err, 7);
    save_float(config->speed.speed_run, 8);
    save_float(config->speed.limiting_Err, 9);
    save_float(config->speed.gyro_damp_Err, 10);
    save_float(config->angle.kp_Angle, 11);
    save_float(config->angle.kd_Angle, 12);
    save_float(config->angle.B_1, 13);
    save_float(config->angle.C_l, 14);
    save_float(config->angle.A_1, 15);
    save_float(config->angle.gyro_feedback_scale, 28);

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
    save_float(clamp_percent_value(config->start.fuya_wall_percent, 70.0f), 27);
}

/**
 * @brief EEPROM 初始化主函数
 * @details
 * 1. 初始化 IAP 模块。
 * 2. 读取 Flash 扇区 0 的全部数据到内存缓冲区。
 * 3. 检查索引 0 处的标志位 `eeprom_init_time`。
 *    - 若不为 1，说明是首次上电或 Flash 被擦除，此时加载默认参数并写入 Flash。
 *    - 若为 1，说明 Flash 中有有效配置，直接从 Flash 读取参数到 `app`。
 */
void eeprom_init(void)
{
    /* 初始化 IAP (In-Application Programming) 模块 */
    iap_init();
    /* 从扇区 0 读取 200 字节到缓冲区 */
    iap_read_buff(0x00, date_buff, sizeof(date_buff));

    /* 预加载默认值到内存结构体（防止读取失败时无初值） */
    eeprom_load_defaults(&app);

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
        /* 若已初始化，则加载 EEPROM 中的真实数据覆盖内存结构体 */
        eeprom_read_config(&app);
    }
}

/**
 * @brief 执行 Flash 刷写操作
 * @details
 * 将当前 `app` 全局变量中的所有参数序列化并保存到 Flash 中。
 * 警告：Flash 擦写会产生极长耗时并可能短暂挂起总线，只能在断脱电机的安全状态下执行，严禁在运行态调用！
 */
void eeprom_flash(void)
{
    eeprom_write_config(&app);
}

/* --- 底层读写辅助函数 --- */

/**
 * @brief 将 int32 类型数据保存到缓冲区指定位置并同步到 Flash
 * @param input 要保存的 32 位整型数据
 * @param value_bit 数据存储的逻辑索引（每个索引占用 4 字节空间）
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
    /* 立即刷写到 Flash 扇区 0 */
    extern_iap_write_buff(0x00, date_buff, sizeof(date_buff));
}

/**
 * @brief 从缓冲区读取 int32 类型数据
 * @param value_bit 数据存储的逻辑索引
 * @return 读取到的 32 位整型数据
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
 * @param input 要保存的 32 位浮点型数据
 * @param value_bit 数据存储的逻辑索引
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
 * @param value_bit 数据存储的逻辑索引
 * @return 读取到的 32 位浮点型数据
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

static float clamp_percent_value(float value, float default_value)
{
    if (value < 0.0f || value > 100.0f)
    {
        return default_value;
    }
    return value;
}
