#include "zf_common_headfile.h"

/* 数据缓冲区覆盖逻辑槽位0~101，每个槽位占4字节。 */
uint8 date_buff[408];
/* EEPROM 初始化标志位，用于判断是否为首次上电（0-首次，1-非首次） */
static uint8 eeprom_init_time = 0;
/* 全局配置结构体实例，运行时所有的参数都从这里读取 */
AppConfig app;

/*
 * EEPROM_CONFIG_VERSION_SLOT 使用扩展区末尾槽位，避开 0~50 的现有和新增参数。
 * 旧车上只写过 init_flag=1 时，版本不匹配会强制刷新默认值，避免按新布局乱读旧数据。
 */
#define EEPROM_CONFIG_VERSION 17L
#define EEPROM_CONFIG_VERSION_SLOT 61

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
 * @details 当检测到 EEPROM 中无有效数据（首次运行）时，使用此函数将硬编码的默认参数填充到 config 结构体中。
 * 这些参数经过预先调试，能保证小车基本的稳定运行。
 * @param config 指向需要填充默认值的配置结构体指针
 */
static void eeprom_load_defaults(AppConfig *config)
{
    /* 启动与基础配置默认值 */
    config->start.start_flag = 1;     /* 默认启动 */
    config->start.element_enable = 1; /* 默认开启整体赛道元素识别 */
    config->start.track_mode = 0;     /* 默认左圆环->圆筒循环 */
    config->start.fuya_xili = 90.00f; /* 默认平地负压百分比 90 */
    config->start.encoder_stop_distance_cm = 3200.0f;
    config->start.element_len = 4;
    config->start.element_seq[0] = TRACK_ELEMENT_SEESAW;   // TRACK_ELEMENT_CROSS
    config->start.element_seq[1] = TRACK_ELEMENT_CYLINDER; // TRACK_ELEMENT_WALL
    config->start.element_seq[2] = TRACK_ELEMENT_RIGHT_RING;
    config->start.element_seq[3] = TRACK_ELEMENT_NONE;
    config->start.element_seq[4] = TRACK_ELEMENT_NONE;
    config->start.element_seq[5] = TRACK_ELEMENT_NONE;
    //	    config->start.element_seq[0] = TRACK_ELEMENT_CYLINDER; // TRACK_ELEMENT_RIGHT_RING
    //    config->start.element_seq[1] = TRACK_ELEMENT_WALL;   // TRACK_ELEMENT_WALL
    //    config->start.element_seq[2] = TRACK_ELEMENT_SEESAW;
    //    config->start.element_seq[3] = TRACK_ELEMENT_RIGHT_RING;
    //    config->start.element_seq[4] = TRACK_ELEMENT_NONE;
    //    config->start.element_seq[5] = TRACK_ELEMENT_NONE;
    /* 转向差速环 PID 默认参数 */
    config->speed.kp_Err = 8.00f;  // 3.50
    config->speed.kd_Err = 12.00f; // 2ms 主环第一版保守微分
    config->speed.gyro_damp_Err = 0.00f;
    config->speed.speed_run = 50.00f;     /* 默认基础速度 60 */
    config->speed.limiting_Err = 800.00f; /* 转向限幅 */
    config->speed.kp2_Err = 0.06f;
    config->speed.diff_enable = 1;
    config->speed.diff_inner_gain = 0.60f;
    config->speed.diff_outer_gain = 0.50f;

    /* 电感偏差解算默认参数 */
    config->angle.kp_Angle = 0.92f;
    config->angle.kd_Angle = 0.78f; // 2ms 主环第一版保守微分
    config->angle.gyro_feedback_scale = 1.50f;
    config->angle.limiting_Angle = 48.00f; // 48
    config->angle.A_1 = 1.00f;
    config->angle.B_1 = 1.20f;
    config->angle.C_l = 0.60f;

    /* 圆环策略默认参数 */
    config->ring.ring_entry_encoder = 10.0;         /* ring->pre_ring编码器积分阈值 */
    config->ring.pre_ring_Gyro_target = 60.00f;     /* pre_ring固定目标角速度 */
    config->ring.pre_ring_Gyroz = 30.00f;           /* pre_ring->in_ring累计转角阈值 */
    config->ring.in_ring_Gyroz = 220.00f;           /* in_ring->pre_out_ring累计转角阈值 220 */
    config->ring.in_ring_encoder = 50.0f;           /* 从pre_ring开始累计的出环打角距离阈值 */
    config->ring.pre_out_ring_Gyro_target = 30.00f; /* pre_out_ring固定目标角速度 25 */
    config->ring.pre_out_ring_Gyroz = 300.00f;      /* pre_out_ring->drive_out_ring累计转角阈值 310 */
    config->ring.drive_out_ring_encoder = 5.0f;     /* 出环向外转向段最小距离（cm） */
    config->ring.control_mode = 1;
    config->ring.profile_select = 0;
    config->ring.profile0_gain_speed_slope = 0.05f;
    config->ring.profiles[0].bias_entry_gain = 4.00f;
    config->ring.profiles[0].bias_exit_gain = 4.00f;
    config->ring.profiles[0].entry_straight_encoder = 5.0f;
    config->ring.profiles[0].bias_entry_yaw = 30.00f;
    config->ring.profiles[0].bias_entry_encoder = 200.00f;
    config->ring.profiles[0].bias_finish_encoder = 1.00f;
    config->ring.profiles[0].target_speed = 50.00f;
    config->ring.profiles[0].adc_a_1 = 1.00f;
    config->ring.profiles[0].adc_b_1 = 1.20f;
    config->ring.profiles[0].adc_c_l = 0.60f;
    config->ring.profiles[0].kp_Err = 8.00f;
    config->ring.profiles[0].kd_Err = 12.00f;
    config->ring.profiles[0].kp2_Err = 0.06f;
    config->ring.profiles[0].kp_Angle = 0.92f;
    config->ring.profiles[0].kd_Angle = 0.78f;
    config->ring.profiles[0].diff_inner_gain = 0.60f;
    config->ring.profiles[0].diff_outer_gain = 0.50f;
    config->ring.profiles[1].bias_entry_gain = 2.00f;
    config->ring.profiles[1].bias_exit_gain = 2.00f;
    config->ring.profiles[1].entry_straight_encoder = 5.0f;
    config->ring.profiles[1].bias_entry_yaw = 30.00f;
    config->ring.profiles[1].bias_entry_encoder = 1.00f;
    config->ring.profiles[1].bias_finish_encoder = 200.00f;
    config->ring.profiles[1].target_speed = 50.00f;
    config->ring.profiles[1].adc_a_1 = 1.20f;
    config->ring.profiles[1].adc_b_1 = 1.00f;
    config->ring.profiles[1].adc_c_l = 0.60f;
    config->ring.profiles[1].kp_Err = 8.00f;
    config->ring.profiles[1].kd_Err = 12.00f;
    config->ring.profiles[1].kp2_Err = 0.01f;
    config->ring.profiles[1].kp_Angle = 0.92f;
    config->ring.profiles[1].kd_Angle = 0.78f;
    config->ring.profiles[1].diff_inner_gain = 0.60f;
    config->ring.profiles[1].diff_outer_gain = 0.50f;

    /* 飞坡策略默认参数 */
    /* 飞坡模式专用 */
    config->fly.fly_speed = 30;              /* LOW 阶段目标速度 */
    config->fly.fly_detect_count = 5;        /* IDLE 入口弱磁确认次数 */
    config->fly.fly_recover_speed = 10;      /* 飞坡 COOLDOWN 恢复速度 */
    config->fly.fly_land_confirm_count = 10; /* 落地回升连续确认次数 */
    config->fly.fly_release_step = 0.3f;     /* 飞坡 COOLDOWN 步长 */
    /* 停止等待模式专用 */
    config->fly.seesaw_detect_count = 5;    /* IDLE 入口命中次数 */
    config->fly.seesaw_wait_count = 10;     /* 停车保持时间，10 * 2ms = 20ms */
    config->fly.seesaw_speed = 20;          /* CREEP 阶段目标速度 */
    config->fly.seesaw_creep_cm = 10.00f;   /* 前挪距离，单位 cm */
    config->fly.seesaw_release_step = 0.6f; /* 停止等待 COOLDOWN 步长 */
    /* 共用 */
    config->fly.seesaw_mode = 1; /* 默认飞坡模式（0=飞坡，1=停止等待） */

    /* 圆桶策略默认参数，当前步骤只入 EEPROM，不切换运行逻辑。 */
    config->cylinder.encoder_target = 300.0f;     /* 后续圆桶里程退出阈值 */
    config->cylinder.ad_both_high_threshold = 45; /* 圆桶双路强信号阈值 */
    config->cylinder.adc_a_1 = 1.20f;             /* 圆桶专用横向主差分权重 */
    config->cylinder.adc_b_1 = 1.00f;             /* 圆桶专用竖向差分权重 */
    config->cylinder.adc_c_l = 0.60f;             /* 保持当前圆桶硬编码 C_l 默认值 */
    config->cylinder.kp_Err = 1.00f;              /* 圆桶专用方向环比例系数 */
    config->cylinder.kd_Err = 1.00f;              /* 圆桶专用方向环微分系数 */
    config->cylinder.exit_slow_speed = 60;        /* 圆桶确认后阶梯减速的最低目标速度 */

    /* 墙面策略默认参数，保持当前固定宏行为不变。 */
    config->wall.slow_speed = 75;         /* 墙面降速目标值 */
    config->wall.slow_time = 150;         /* 墙面降速持续时间，2ms * 150 = 300ms */
    config->wall.timing_count = 500;      /* 墙面下墙计时，2ms * 500 = 1000ms */
    config->wall.encoder_target = 250.0f; /* 墙面退出编码器积分阈值 */

    /* 双十字策略默认参数 */
    config->cross.encoder_target = 300.0f; /* 双十字退出编码器积分阈值 */
    config->cross.adc_a_1 = 1.00f;         /* 双十字横向主差分默认权重 */
    config->cross.adc_b_1 = 1.20f;         /* 双十字竖向差分默认权重 */
    config->cross.adc_c_l = 0.60f;         /* 双十字分母补偿默认权重 */
}

/**
 * @brief 从 EEPROM 缓冲区解析配置数据到结构体
 * @details 按照固定的索引顺序（value_bit），将 date_buff 中的二进制数据还原为结构体成员变量。
 * @param config 指向接收数据的配置结构体指针
 */
static void eeprom_read_config(AppConfig *config)
{
    config->start.start_flag = (int16)read_int(1);
    config->start.element_enable = (int16)read_int(2);
    config->start.encoder_stop_distance_cm = read_float(63);

    config->speed.kp_Err = read_float(4);
    config->start.fuya_xili = read_float(5);
    config->speed.kd_Err = read_float(6);
    config->speed.kp2_Err = read_float(7);
    config->speed.speed_run = read_float(8);
    config->speed.limiting_Err = read_float(9);
    config->speed.gyro_damp_Err = read_float(10);
    config->speed.diff_enable = (int16)read_int(27);
    config->speed.diff_inner_gain = read_float(60);
    config->speed.diff_outer_gain = read_float(62);

    /* 槽位 3 复用为角速度内环限幅，保留原 EEPROM 布局。 */
    config->angle.limiting_Angle = read_float(3);

    /* 11/12 映射为角速度内环参数，保留原 EEPROM 布局以兼容旧数据 */
    config->angle.kp_Angle = read_float(11);
    config->angle.kd_Angle = read_float(12);
    config->angle.B_1 = read_float(13);
    config->angle.C_l = read_float(14);
    config->angle.A_1 = read_float(15);
    config->angle.gyro_feedback_scale = read_float(28);

    config->ring.ring_entry_encoder = read_float(16);
    config->ring.pre_ring_Gyro_target = read_float(17);
    config->ring.pre_ring_Gyroz = read_float(18);
    config->ring.in_ring_Gyroz = read_float(19);
    config->ring.in_ring_encoder = read_float(64);
    config->ring.pre_out_ring_Gyro_target = read_float(20);
    config->ring.pre_out_ring_Gyroz = read_float(21);
    config->ring.drive_out_ring_encoder = read_float(51);
    config->ring.control_mode = (int16)read_int(100);
    config->ring.profile0_gain_speed_slope = read_float(101);
    config->ring.profiles[0].bias_entry_gain = read_float(65);
    config->ring.profiles[0].bias_exit_gain = read_float(88);
    config->ring.profiles[0].entry_straight_encoder = read_float(98);
    config->ring.profiles[0].bias_entry_yaw = read_float(66);
    config->ring.profiles[0].bias_entry_encoder = read_float(67);
    config->ring.profiles[0].kp_Err = read_float(68);
    config->ring.profiles[0].bias_finish_encoder = read_float(69);
    config->ring.profiles[0].adc_a_1 = read_float(70);
    config->ring.profiles[0].adc_b_1 = read_float(71);
    config->ring.profiles[0].adc_c_l = read_float(72);
    config->ring.profiles[0].kd_Err = read_float(73);
    config->ring.profiles[0].kp2_Err = read_float(74);
    config->ring.profiles[0].target_speed = read_float(75);
    config->ring.profiles[0].kp_Angle = read_float(90);
    config->ring.profiles[0].kd_Angle = read_float(91);
    config->ring.profiles[0].diff_inner_gain = read_float(92);
    config->ring.profiles[0].diff_outer_gain = read_float(93);
    config->ring.profiles[1].bias_entry_gain = read_float(76);
    config->ring.profiles[1].bias_exit_gain = read_float(89);
    config->ring.profiles[1].entry_straight_encoder = read_float(99);
    config->ring.profiles[1].bias_entry_yaw = read_float(77);
    config->ring.profiles[1].bias_entry_encoder = read_float(78);
    config->ring.profiles[1].bias_finish_encoder = read_float(79);
    config->ring.profiles[1].adc_a_1 = read_float(80);
    config->ring.profiles[1].adc_b_1 = read_float(81);
    config->ring.profiles[1].adc_c_l = read_float(82);
    config->ring.profiles[1].kp_Err = read_float(83);
    config->ring.profiles[1].kd_Err = read_float(84);
    config->ring.profiles[1].kp2_Err = read_float(85);
    config->ring.profiles[1].target_speed = read_float(86);
    config->ring.profiles[1].kp_Angle = read_float(94);
    config->ring.profiles[1].kd_Angle = read_float(95);
    config->ring.profiles[1].diff_inner_gain = read_float(96);
    config->ring.profiles[1].diff_outer_gain = read_float(97);
    config->ring.profile_select = (int16)read_int(87);

    config->fly.fly_speed = (int)read_int(22);
    config->fly.fly_detect_count = (int)read_int(23);
    config->fly.seesaw_detect_count = (int)read_int(24);
    config->fly.seesaw_mode = (int16)read_int(25);
    config->fly.seesaw_wait_count = (int)read_int(37);
    config->fly.fly_recover_speed = (int)read_int(38);
    config->fly.fly_release_step = read_float(39);
    config->fly.seesaw_creep_cm = read_float(40);
    config->fly.seesaw_speed = (int)read_int(53);
    config->fly.fly_land_confirm_count = (int)read_int(54);
    config->fly.seesaw_release_step = read_float(55);
    config->start.track_mode = (int16)read_int(29);
    config->start.element_len = (int)read_int(30);
    config->start.element_seq[0] = (int)read_int(31);
    config->start.element_seq[1] = (int)read_int(32);
    config->start.element_seq[2] = (int)read_int(33);
    config->start.element_seq[3] = (int)read_int(34);
    config->start.element_seq[4] = (int)read_int(35);
    config->start.element_seq[5] = (int)read_int(36);

    config->cylinder.encoder_target = read_float(41);
    config->cylinder.ad_both_high_threshold = (int)read_int(42);
    config->cylinder.adc_a_1 = read_float(43);
    config->cylinder.adc_b_1 = read_float(44);
    config->cylinder.adc_c_l = read_float(45);
    config->cylinder.kp_Err = read_float(46);
    config->cylinder.kd_Err = read_float(47);
    config->cylinder.exit_slow_speed = (int)read_int(26);

    config->wall.slow_speed = (int)read_int(48);
    config->wall.slow_time = (int)read_int(49);
    config->wall.timing_count = (int)read_int(50);
    config->wall.encoder_target = read_float(52);

    config->cross.encoder_target = read_float(56);
    config->cross.adc_a_1 = read_float(57);
    config->cross.adc_b_1 = read_float(58);
    config->cross.adc_c_l = read_float(59);
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
    save_int(config->start.element_enable, 2);
    save_float(config->start.encoder_stop_distance_cm, 63);
    save_float(config->angle.limiting_Angle, 3);

    save_float(config->speed.kp_Err, 4);
    save_float(config->start.fuya_xili, 5);
    save_float(config->speed.kd_Err, 6);
    save_float(config->speed.kp2_Err, 7);
    save_float(config->speed.speed_run, 8);
    save_float(config->speed.limiting_Err, 9);
    save_float(config->speed.gyro_damp_Err, 10);
    save_int(config->speed.diff_enable, 27);
    save_float(config->speed.diff_inner_gain, 60);
    save_float(config->speed.diff_outer_gain, 62);
    save_float(config->angle.kp_Angle, 11);
    save_float(config->angle.kd_Angle, 12);
    save_float(config->angle.B_1, 13);
    save_float(config->angle.C_l, 14);
    save_float(config->angle.A_1, 15);
    save_float(config->angle.gyro_feedback_scale, 28);

    save_float(config->ring.ring_entry_encoder, 16);
    save_float(config->ring.pre_ring_Gyro_target, 17);
    save_float(config->ring.pre_ring_Gyroz, 18);
    save_float(config->ring.in_ring_Gyroz, 19);
    save_float(config->ring.in_ring_encoder, 64);
    save_float(config->ring.pre_out_ring_Gyro_target, 20);
    save_float(config->ring.pre_out_ring_Gyroz, 21);
    save_float(config->ring.drive_out_ring_encoder, 51);
    save_int(config->ring.control_mode, 100);
    save_float(config->ring.profile0_gain_speed_slope, 101);
    save_float(config->ring.profiles[0].bias_entry_gain, 65);
    save_float(config->ring.profiles[0].bias_exit_gain, 88);
    save_float(config->ring.profiles[0].entry_straight_encoder, 98);
    save_float(config->ring.profiles[0].bias_entry_yaw, 66);
    save_float(config->ring.profiles[0].bias_entry_encoder, 67);
    save_float(config->ring.profiles[0].kp_Err, 68);
    save_float(config->ring.profiles[0].bias_finish_encoder, 69);
    save_float(config->ring.profiles[0].adc_a_1, 70);
    save_float(config->ring.profiles[0].adc_b_1, 71);
    save_float(config->ring.profiles[0].adc_c_l, 72);
    save_float(config->ring.profiles[0].kd_Err, 73);
    save_float(config->ring.profiles[0].kp2_Err, 74);
    save_float(config->ring.profiles[0].target_speed, 75);
    save_float(config->ring.profiles[0].kp_Angle, 90);
    save_float(config->ring.profiles[0].kd_Angle, 91);
    save_float(config->ring.profiles[0].diff_inner_gain, 92);
    save_float(config->ring.profiles[0].diff_outer_gain, 93);
    save_float(config->ring.profiles[1].bias_entry_gain, 76);
    save_float(config->ring.profiles[1].bias_exit_gain, 89);
    save_float(config->ring.profiles[1].entry_straight_encoder, 99);
    save_float(config->ring.profiles[1].bias_entry_yaw, 77);
    save_float(config->ring.profiles[1].bias_entry_encoder, 78);
    save_float(config->ring.profiles[1].bias_finish_encoder, 79);
    save_float(config->ring.profiles[1].adc_a_1, 80);
    save_float(config->ring.profiles[1].adc_b_1, 81);
    save_float(config->ring.profiles[1].adc_c_l, 82);
    save_float(config->ring.profiles[1].kp_Err, 83);
    save_float(config->ring.profiles[1].kd_Err, 84);
    save_float(config->ring.profiles[1].kp2_Err, 85);
    save_float(config->ring.profiles[1].target_speed, 86);
    save_float(config->ring.profiles[1].kp_Angle, 94);
    save_float(config->ring.profiles[1].kd_Angle, 95);
    save_float(config->ring.profiles[1].diff_inner_gain, 96);
    save_float(config->ring.profiles[1].diff_outer_gain, 97);
    save_int(config->ring.profile_select, 87);

    save_int(config->fly.fly_speed, 22);
    save_int(config->fly.fly_detect_count, 23);
    save_int(config->fly.seesaw_detect_count, 24);
    save_int(config->fly.seesaw_mode, 25);
    save_int(config->fly.seesaw_wait_count, 37);
    save_int(config->fly.fly_recover_speed, 38);
    save_float(config->fly.fly_release_step, 39);
    save_float(config->fly.seesaw_creep_cm, 40);
    save_int(config->fly.seesaw_speed, 53);
    save_int(config->fly.fly_land_confirm_count, 54);
    save_float(config->fly.seesaw_release_step, 55);
    save_int(config->start.track_mode, 29);
    save_int(config->start.element_len, 30);
    save_int(config->start.element_seq[0], 31);
    save_int(config->start.element_seq[1], 32);
    save_int(config->start.element_seq[2], 33);
    save_int(config->start.element_seq[3], 34);
    save_int(config->start.element_seq[4], 35);
    save_int(config->start.element_seq[5], 36);

    save_float(config->cylinder.encoder_target, 41);
    save_int(config->cylinder.ad_both_high_threshold, 42);
    save_float(config->cylinder.adc_a_1, 43);
    save_float(config->cylinder.adc_b_1, 44);
    save_float(config->cylinder.adc_c_l, 45);
    save_float(config->cylinder.kp_Err, 46);
    save_float(config->cylinder.kd_Err, 47);
    save_int(config->cylinder.exit_slow_speed, 26);

    save_int(config->wall.slow_speed, 48);
    save_int(config->wall.slow_time, 49);
    save_int(config->wall.timing_count, 50);
    save_float(config->wall.encoder_target, 52);

    save_float(config->cross.encoder_target, 56);
    save_float(config->cross.adc_a_1, 57);
    save_float(config->cross.adc_b_1, 58);
    save_float(config->cross.adc_c_l, 59);

    save_int(EEPROM_CONFIG_VERSION, EEPROM_CONFIG_VERSION_SLOT);
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
    int32 config_version;

    /* 初始化 IAP (In-Application Programming) 模块 */
    iap_init();
    /* 从扇区 0 读取完整配置到缓冲区。 */
    iap_read_buff(0x00, date_buff, sizeof(date_buff));

    /* 预加载默认值到内存结构体（防止读取失败时无初值） */
    eeprom_load_defaults(&app);

    /* 检查索引 0 的标志位，判断是否为有效配置 */
    eeprom_init_time = (uint8)read_int(0);
    config_version = read_int(EEPROM_CONFIG_VERSION_SLOT);

    if (eeprom_init_time != 1 || config_version != EEPROM_CONFIG_VERSION)
    {
        /* 旧布局只保存了 init_flag，新版本必须整体刷新，避免新增槽位读到错位参数。 */
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
    uint16 begin = value_bit * 4;
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
    uint16 begin = value_bit * 4;
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
    uint16 begin = value_bit * 4;
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
    uint16 begin = value_bit * 4;
    float output;
    uint8 *p = (uint8 *)&output;

    for (i = 0; i < 4; i++)
    {
        *(p + i) = date_buff[begin++];
    }
    return output;
}
