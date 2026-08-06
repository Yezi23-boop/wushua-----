#include "zf_common_headfile.h"

/* 数据缓冲区覆盖逻辑槽位0~101，每个槽位占4字节。 */
uint8 date_buff[408];
/* EEPROM 初始化标志位，用于判断是否为首次上电（0-首次，1-非首次） */
static uint8 eeprom_init_time = 0;
/* 全局配置结构体实例，运行时所有的参数都从这里读取 */
AppConfig app;

/*
 * 槽位布局：0-初始化标志，1~72-参数表按菜单显示顺序占用，73-版本号。
 * 1~6 元素序列 E1~E6，7~11 START，12~17 CTRL，
 * 18~23 MODEL，24~26 DIFF，27~45 RING，46~53 CYLINDER，54~57 WALL，
 * 58~68 FLY，69~72 CROSS。
 * 版本不匹配会强制刷新默认值，避免按新布局乱读旧数据。
 */
#define EEPROM_CONFIG_VERSION 24L
#define EEPROM_CONFIG_VERSION_SLOT 73

/* 内部私有函数声明 */
typedef enum
{
    EEPROM_VALUE_INT16 = 0,
    EEPROM_VALUE_FLOAT
} EepromValueType;

typedef struct
{
    void *target;
    uint8 slot;
    uint8 type;
    float default_float;
    int16 default_int;
} EepromConfigItem;

#define EEPROM_INT(member, index, value) \
    {&app.member, index, EEPROM_VALUE_INT16, 0.0f, value}
#define EEPROM_FLOAT(member, index, value) \
    {&app.member, index, EEPROM_VALUE_FLOAT, value, 0}

static const EepromConfigItem eeprom_config_items[] = {
    /* --- 元素序列（ELEM 页面，放最前便于现场调整序列） --- */
    EEPROM_INT(start.element_seq[0], 1, TRACK_ELEMENT_WALL),       // 元素序列槽位0
    EEPROM_INT(start.element_seq[1], 2, TRACK_ELEMENT_LEFT_RING),  // 元素序列槽位1
    EEPROM_INT(start.element_seq[2], 3, TRACK_ELEMENT_CYLINDER),   // 元素序列槽位2
    EEPROM_INT(start.element_seq[3], 4, TRACK_ELEMENT_RIGHT_RING), // 元素序列槽位3
    EEPROM_INT(start.element_seq[4], 5, TRACK_ELEMENT_NONE),       // 元素序列槽位4
    EEPROM_INT(start.element_seq[5], 6, TRACK_ELEMENT_NONE),       // 元素序列槽位5
    /* --- START 页面 --- */
    EEPROM_INT(start.start_flag, 7, 1),                         // 启动标志：1-运行，0-待机
    EEPROM_INT(start.element_enable, 8, 1),                     // 赛道元素识别总开关
    EEPROM_FLOAT(start.fuya_xili, 9, 70.0f),                    // 平地负压吸附百分比
    EEPROM_FLOAT(angle.gyro_feedback_scale, 10, 1.80f),         // 角速度反馈缩放，匹配gyro_z量级
    EEPROM_FLOAT(start.encoder_stop_distance_cm, 11, 22000.0f), // 上电累计里程达到后停车
    /* --- CTRL 页面 --- */
    EEPROM_FLOAT(speed.kp_Err, 12, 5.55f),        // 转向环比例系数Kp
    EEPROM_FLOAT(speed.kd_Err, 13, 9.0f),         // 转向环微分系数Kd
    EEPROM_FLOAT(speed.gyro_damp_Err, 14, 0.0f),  // 转向环陀螺仪阻尼，抑制高速摆振
    EEPROM_FLOAT(speed.speed_run, 15, 70.0f),     // 赛道基础运行速度
    EEPROM_FLOAT(speed.limiting_Err, 16, 800.0f), // 转向输出限幅
    EEPROM_FLOAT(speed.kp2_Err, 17, 0.012f),      // 转向环二次项非线性增强系数
    /* --- MODEL 页面 --- */
    EEPROM_FLOAT(angle.kp_Angle, 18, 1.30f),       // 角速度内环比例系数Kp
    EEPROM_FLOAT(angle.kd_Angle, 19, 1.50f),       // 角速度内环微分系数Kd
    EEPROM_FLOAT(angle.limiting_Angle, 20, 60.0f), // 角速度内环输出限幅
    EEPROM_FLOAT(angle.A_1, 21, 1.00f),            // 横向主差分权重
    EEPROM_FLOAT(angle.B_1, 22, 1.20f),            // 竖向差分权重，斜入/斜出姿态修正
    EEPROM_FLOAT(angle.C_l, 23, 0.60f),            // 分母补偿权重，弱信号时抑制偏差放大
    /* --- DIFF 页面 --- */
    EEPROM_INT(speed.diff_enable, 24, 0),           // 非线性内外轮差速开关
    EEPROM_FLOAT(speed.diff_inner_gain, 25, 0.60f), // 差速分配内轮减速增益
    EEPROM_FLOAT(speed.diff_outer_gain, 26, 0.50f), // 差速分配外轮增速增益
    /* --- RING 页面 --- */
    EEPROM_FLOAT(ring.profile.entry_straight_encoder, 27, 5.0f), // 入口识别后零角速度直走距离
    EEPROM_FLOAT(ring.gain_speed_slope, 28, 0.08f),              // 进环增益随目标速度的补偿斜率
    /* --- RING ENTRY 子页面 --- */
    EEPROM_FLOAT(ring.profile.bias_entry_gain, 29, 2.10f),       // 进环阶段同侧两路电感放大倍数
    EEPROM_FLOAT(ring.profile.bias_exit_gain, 30, 1.00f),        // 出环阶段对侧两路电感放大倍数
    EEPROM_FLOAT(ring.profile.bias_entry_yaw, 31, 30.0f),        // 结束进环偏置的累计转角阈值
    EEPROM_FLOAT(ring.profile.bias_entry_encoder, 32, 1.00f),    // 结束进环偏置的里程阈值
    EEPROM_FLOAT(ring.profile.bias_finish_encoder, 33, 100.00f), // 出环判定的里程阈值
    EEPROM_FLOAT(ring.profile.bias_finish_yaw, 34, 360.0f),      // 出环满圈角度积分阈值，与里程双条件确认
    EEPROM_FLOAT(ring.profile.target_speed, 35, 70.0f),          // 圆环进环/环内/出环目标速度
    /* --- RING CTRL 子页面 --- */
    EEPROM_FLOAT(ring.profile.adc_a_1, 36, 1.0f),  // 圆环阶段横向主差分权重
    EEPROM_FLOAT(ring.profile.adc_b_1, 37, 1.40f), // 圆环阶段辅助电感差分权重
    EEPROM_FLOAT(ring.profile.adc_c_l, 38, 0.60f), // 圆环阶段分母补偿权重
    EEPROM_FLOAT(ring.profile.kp_Err, 39, 5.50f),  // 圆环阶段方向环比例系数
    EEPROM_FLOAT(ring.profile.kd_Err, 40, 12.0f),  // 圆环阶段方向环微分系数
    EEPROM_FLOAT(ring.profile.kp2_Err, 41, 0.02f), // 圆环阶段方向环非线性增强系数
    /* --- RING DRIVE 子页面 --- */
    EEPROM_FLOAT(ring.profile.kp_Angle, 42, 0.90f),        // 圆环阶段角速度内环比例系数
    EEPROM_FLOAT(ring.profile.kd_Angle, 43, 0.78f),        // 圆环阶段角速度内环微分系数
    EEPROM_FLOAT(ring.profile.diff_inner_gain, 44, 0.60f), // 圆环阶段内轮减速增益
    EEPROM_FLOAT(ring.profile.diff_outer_gain, 45, 0.50f), // 圆环阶段外轮增速增益
    /* --- CYLINDER 页面 --- */
    EEPROM_FLOAT(cylinder.encoder_target, 46, 300.0f),   // 圆桶编码器积分退出阈值
    EEPROM_INT(cylinder.ad_both_high_threshold, 47, 45), // 圆桶双路强信号识别阈值
    EEPROM_FLOAT(cylinder.adc_a_1, 48, 1.20f),           // 圆桶专用横向主差分权重
    EEPROM_FLOAT(cylinder.adc_b_1, 49, 8.00f),           // 圆桶专用竖向差分权重
    EEPROM_FLOAT(cylinder.adc_c_l, 50, 0.60f),           // 圆桶专用分母补偿权重
    EEPROM_INT(cylinder.exit_slow_speed, 51, 60),        // 圆桶确认后阶梯减速的最低目标速度
    EEPROM_FLOAT(cylinder.kp_Err, 52, 1.0f),             // 圆桶专用方向环比例系数
    EEPROM_FLOAT(cylinder.kd_Err, 53, 1.0f),             // 圆桶专用方向环微分系数
    /* --- WALL 页面 --- */
    EEPROM_INT(wall.slow_speed, 54, 75),           // 墙面阶段降速目标值
    EEPROM_INT(wall.slow_time, 55, 150),           // 墙面阶段降速持续时间，×2ms
    EEPROM_INT(wall.timing_count, 56, 500),        // 墙面阶段下墙计时，×2ms
    EEPROM_FLOAT(wall.encoder_target, 57, 250.0f), // 墙面退出编码器积分阈值
    /* --- FLY 页面（飞坡参数 + 停止等待参数） --- */
    EEPROM_INT(fly.seesaw_mode, 58, 1),              // 飞坡模式选择：0-飞坡，1-停止等待
    EEPROM_INT(fly.fly_speed, 59, 30),               // 飞坡LOW阶段目标速度
    EEPROM_INT(fly.fly_detect_count, 60, 5),         // 飞坡入口弱磁确认次数
    EEPROM_INT(fly.fly_recover_speed, 61, 10),       // 飞坡COOLDOWN恢复速度
    EEPROM_FLOAT(fly.fly_release_step, 62, 0.3f),    // 飞坡COOLDOWN调节步长
    EEPROM_INT(fly.fly_land_confirm_count, 63, 10),  // 飞坡落地回升连续确认次数
    EEPROM_INT(fly.seesaw_speed, 64, 15),            // 停止等待模式CREEP阶段目标速度
    EEPROM_INT(fly.seesaw_detect_count, 65, 5),      // 跷跷板入口命中次数
    EEPROM_INT(fly.seesaw_wait_count, 66, 10),       // 停止等待模式停车等待时间，×2ms
    EEPROM_FLOAT(fly.seesaw_creep_cm, 67, 20.0f),    // 停止等待模式前挪距离
    EEPROM_FLOAT(fly.seesaw_release_step, 68, 0.2f), // 停止等待COOLDOWN调节步长
    /* --- CROSS 页面 --- */
    EEPROM_FLOAT(cross.encoder_target, 69, 300.0f), // 双十字退出编码器积分阈值
    EEPROM_FLOAT(cross.adc_a_1, 70, 1.0f),          // 双十字专用横向主差分权重
    EEPROM_FLOAT(cross.adc_b_1, 71, 1.20f),         // 双十字专用竖向差分权重
    EEPROM_FLOAT(cross.adc_c_l, 72, 0.60f)};        // 双十字专用分母补偿权重

#define EEPROM_CONFIG_ITEM_COUNT \
    ((uint8)(sizeof(eeprom_config_items) / sizeof(eeprom_config_items[0])))

static void eeprom_load_defaults(void);
static void eeprom_read_config(void);
static void eeprom_write_config(void);
static void eeprom_store_int(int32 input, uint8 value_bit);
static int32 eeprom_load_int(uint8 value_bit);
static void eeprom_store_float(float input, uint8 value_bit);
static float eeprom_load_float(uint8 value_bit);

/** @brief 将描述表中的默认值加载到全局配置。 */
static void eeprom_load_defaults(void)
{
    const EepromConfigItem *item;
    uint8 i;

    for (i = 0; i < EEPROM_CONFIG_ITEM_COUNT; i++)
    {
        item = &eeprom_config_items[i];
        if (item->type == EEPROM_VALUE_FLOAT)
            *((float *)item->target) = item->default_float;
        else
            *((int16 *)item->target) = item->default_int;
    }
}

/** @brief 将 EEPROM 缓冲区按描述表恢复到全局配置。 */
static void eeprom_read_config(void)
{
    const EepromConfigItem *item;
    uint8 i;

    for (i = 0; i < EEPROM_CONFIG_ITEM_COUNT; i++)
    {
        item = &eeprom_config_items[i];
        if (item->type == EEPROM_VALUE_FLOAT)
            *((float *)item->target) = eeprom_load_float(item->slot);
        else
            *((int16 *)item->target) = (int16)eeprom_load_int(item->slot);
    }
}

/** @brief 将全局配置按描述表更新缓冲区后仅刷写一次 Flash。 */
static void eeprom_write_config(void)
{
    const EepromConfigItem *item;
    uint8 i;

    for (i = 0; i < EEPROM_CONFIG_ITEM_COUNT; i++)
    {
        item = &eeprom_config_items[i];
        if (item->type == EEPROM_VALUE_FLOAT)
            eeprom_store_float(*((float *)item->target), item->slot);
        else
            eeprom_store_int(*((int16 *)item->target), item->slot);
    }
    eeprom_store_int(EEPROM_CONFIG_VERSION, EEPROM_CONFIG_VERSION_SLOT);
    extern_iap_write_buff(0x00, date_buff, sizeof(date_buff));
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
    eeprom_load_defaults();

    /* 检查索引 0 的标志位，判断是否为有效配置 */
    eeprom_init_time = (uint8)eeprom_load_int(0);
    config_version = eeprom_load_int(EEPROM_CONFIG_VERSION_SLOT);

    if (eeprom_init_time != 1 || config_version != EEPROM_CONFIG_VERSION)
    {
        /* 旧布局只保存了 init_flag，新版本必须整体刷新，避免新增槽位读到错位参数。 */
        eeprom_init_time = 1;
        eeprom_store_int(eeprom_init_time, 0);
        eeprom_flash();
    }
    else
    {
        /* 若已初始化，则加载 EEPROM 中的真实数据覆盖内存结构体 */
        eeprom_read_config();
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
    eeprom_write_config();
}

/* --- 底层读写辅助函数 --- */

/** @brief 将 32 位整数编码到指定 EEPROM 槽位的 RAM 缓冲区。 */
static void eeprom_store_int(int32 input, uint8 value_bit)
{
    uint8 i;
    uint8 *source;
    uint16 begin;

    source = (uint8 *)&input;
    begin = (uint16)value_bit * 4u;
    for (i = 0; i < 4; i++)
        date_buff[begin++] = source[i];
}

/** @brief 从指定 EEPROM 槽位的 RAM 缓冲区解码 32 位整数。 */
static int32 eeprom_load_int(uint8 value_bit)
{
    uint8 i;
    uint8 *target;
    uint16 begin;
    int32 output;

    target = (uint8 *)&output;
    begin = (uint16)value_bit * 4u;
    for (i = 0; i < 4; i++)
        target[i] = date_buff[begin++];
    return output;
}

/** @brief 将浮点数编码到指定 EEPROM 槽位的 RAM 缓冲区。 */
static void eeprom_store_float(float input, uint8 value_bit)
{
    uint8 i;
    uint8 *source;
    uint16 begin;

    source = (uint8 *)&input;
    begin = (uint16)value_bit * 4u;
    for (i = 0; i < 4; i++)
        date_buff[begin++] = source[i];
}

/** @brief 从指定 EEPROM 槽位的 RAM 缓冲区解码浮点数。 */
static float eeprom_load_float(uint8 value_bit)
{
    uint8 i;
    uint8 *target;
    uint16 begin;
    float output;

    target = (uint8 *)&output;
    begin = (uint16)value_bit * 4u;
    for (i = 0; i < 4; i++)
        target[i] = date_buff[begin++];
    return output;
}
