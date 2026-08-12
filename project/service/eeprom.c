#include "zf_common_headfile.h"

/* 数据缓冲区覆盖逻辑槽位0~101，每个槽位占4字节。 */
uint8 date_buff[408];
/* 参数存储起始扇区：一扇区 512 字节，date_buff 占 408 字节不跨扇区。
 * 换扇区后首次上电新扇区全 0xFF，初始化标志判为非首次，自动写入全套默认值，
 * 旧扇区里的现场参数不会携带过来，需重新调参保存。 */
#define EEPROM_CONFIG_SECTOR_ADDR 0x200u
/* 初始化标志槽位：放缓冲区最末（字节404~407），整扇区写入时排在所有参数之后。
 * 写中途掉电时标志仍为 0xFF，上电按首次初始化重写全套默认值，
 * 避免“标志有效+参数半截 0xFF”的脏数据被直接加载进控制链。 */
#define EEPROM_INIT_FLAG_SLOT 101
/* EEPROM 初始化标志位，用于判断是否为首次上电（0-首次，1-非首次） */
static uint8 eeprom_init_time = 0;
/* 全局配置结构体实例，运行时所有的参数都从这里读取 */
AppConfig app;

/*
 * 槽位布局：1~85-参数表按菜单显示顺序占用，101-初始化标志（缓冲区最末，最后写入）。
 * 1~6 元素序列 E1~E6，7~11 START，12~17 CTRL，
 * 18~23 MODEL，24~29 DIFF，30~48 RING，49~56 CYLINDER，57~60 WALL，
 * 61~71 FLY，72~78 CROSS，79~85 CROSSS。
 * 版本迁移机制已取消：改默认值不再刷写旧车，现场调参不会被冲掉。
 * 注意：调整槽位布局后旧数据会按新布局误读，需现场手动纠正参数。
 */

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
///* 元素编号与默认序列常量统一收拢在这里，避免再多一层薄配置头。 */
// #define TRACK_ELEMENT_NONE 0         /**< 空槽位，用于跳过或现场临时关闭某个序列位置。 */
// #define TRACK_ELEMENT_LEFT_RING 1    /**< 左圆环流程显示值。 */
// #define TRACK_ELEMENT_RIGHT_RING 2   /**< 右圆环流程显示值。 */
// #define TRACK_ELEMENT_CYLINDER 3     /**< 圆桶流程显示值。 */
// #define TRACK_ELEMENT_WALL 4         /**< 墙面流程显示值。 */
// #define TRACK_ELEMENT_SEESAW 5       /**< 跷跷板流程显示值。 */
// #define TRACK_ELEMENT_CROSS 6        /**< 双十字流程显示值。 */
// #define TRACK_ELEMENT_CROSS_SINGLE 7 /**< 单十字流程显示值。 */
static const EepromConfigItem eeprom_config_items[] = {
    /* --- 元素序列（ELEM 页面，放最前便于现场调整序列） --- */
    EEPROM_INT(start.element_seq[0], 1, TRACK_ELEMENT_NONE), // 元素序列槽位0
    EEPROM_INT(start.element_seq[1], 2, TRACK_ELEMENT_NONE), // 元素序列槽位1
    EEPROM_INT(start.element_seq[2], 3, TRACK_ELEMENT_NONE), // 元素序列槽位2
    EEPROM_INT(start.element_seq[3], 4, TRACK_ELEMENT_NONE), // 元素序列槽位3
    EEPROM_INT(start.element_seq[4], 5, TRACK_ELEMENT_NONE), // 元素序列槽位4
    EEPROM_INT(start.element_seq[5], 6, TRACK_ELEMENT_NONE), // 元素序列槽位5
    /* --- START 页面 --- */
    EEPROM_INT(start.start_flag, 7, 1),                         // 启动标志：1-运行，0-待机
    EEPROM_INT(start.element_enable, 8, 1),                     // 赛道元素识别总开关
    EEPROM_FLOAT(start.fuya_xili, 9, 90.0f),                    // 平地负压吸附百分比
    EEPROM_FLOAT(angle.gyro_feedback_scale, 10, 1.80f),         // 角速度反馈缩放，匹配gyro_z量级
    EEPROM_FLOAT(start.encoder_stop_distance_cm, 11, 12000.0f), // 上电累计里程达到后停车
    /* --- CTRL 页面 --- */
    EEPROM_FLOAT(speed.kp_Err, 12, 5.50f),        // 转向环比例系数Kp
    EEPROM_FLOAT(speed.kd_Err, 13, 9.0f),         // 转向环微分系数Kd
    EEPROM_FLOAT(speed.gyro_damp_Err, 14, 0.0f),  // 转向环陀螺仪阻尼，抑制高速摆振
    EEPROM_FLOAT(speed.speed_run, 15, 50.0f),     // 赛道基础运行速度
    EEPROM_FLOAT(speed.limiting_Err, 16, 800.0f), // 转向输出限幅
    EEPROM_FLOAT(speed.kp2_Err, 17, 0.010f),      // 转向环二次项非线性增强系数
    /* --- MODEL 页面 --- */
    EEPROM_FLOAT(angle.kp_Angle, 18, 1.40f),       // 角速度内环比例系数Kp
    EEPROM_FLOAT(angle.kd_Angle, 19, 1.52f),       // 角速度内环微分系数Kd
    EEPROM_FLOAT(angle.limiting_Angle, 20, 48.0f), // 角速度内环输出限幅
    EEPROM_FLOAT(angle.A_1, 21, 1.00f),            // 横向主差分权重
    EEPROM_FLOAT(angle.B_1, 22, 1.20f),            // 竖向差分权重，斜入/斜出姿态修正
    EEPROM_FLOAT(angle.C_l, 23, 0.60f),            // 分母补偿权重，弱信号时抑制偏差放大
    /* --- DIFF 页面（槽位按菜单显示顺序：diff_en/inner_g/outer_g/strong_sm/sc_angle/strong_en） --- */
    EEPROM_INT(speed.diff_enable, 24, 0),                // 非线性内外轮差速开关
    EEPROM_FLOAT(speed.diff_inner_gain, 25, 0.60f),      // 差速分配内轮减速增益
    EEPROM_FLOAT(speed.diff_outer_gain, 26, 0.50f),      // 差速分配外轮增速增益
    EEPROM_FLOAT(angle.strong_signal_sum, 27, 120.0f),   // 强信号姿态锁定阈值，四路和超过即锁定航向
    EEPROM_FLOAT(angle.strong_correct_angle, 28, 10.0f), // 强信号区反向修正角速度，带符号
    EEPROM_INT(angle.strong_signal_enable, 29, 0),       // 强信号姿态锁定总开关：1-开启，0-关闭
    /* --- RING 页面 --- */
    EEPROM_FLOAT(ring.profile.entry_straight_encoder, 30, 5.0f), // 入口识别后零角速度直走距离
    EEPROM_FLOAT(ring.gain_speed_slope, 31, 0.09f),              // 进环增益随目标速度的补偿斜率
    /* --- RING ENTRY 子页面 --- */
    EEPROM_FLOAT(ring.profile.bias_entry_gain, 32, 2.30f),       // 进环阶段同侧两路电感放大倍数
    EEPROM_FLOAT(ring.profile.bias_exit_gain, 33, 1.00f),        // 出环阶段对侧两路电感放大倍数
    EEPROM_FLOAT(ring.profile.bias_entry_yaw, 34, 40.0f),        // 结束进环偏置的累计转角阈值
    EEPROM_FLOAT(ring.profile.bias_entry_encoder, 35, 3.00f),    // 结束进环偏置的里程阈值
    EEPROM_FLOAT(ring.profile.bias_finish_encoder, 36, 100.00f), // 出环判定的里程阈值
    EEPROM_FLOAT(ring.profile.bias_finish_yaw, 37, 360.0f),      // 出环满圈角度积分阈值，与里程双条件确认
    EEPROM_FLOAT(ring.profile.target_speed, 38, 60.0f),          // 圆环进环/环内/出环目标速度
    /* --- RING CTRL 子页面 --- */
    EEPROM_FLOAT(ring.profile.adc_a_1, 39, 1.0f),  // 圆环阶段横向主差分权重
    EEPROM_FLOAT(ring.profile.adc_b_1, 40, 1.20f), // 圆环阶段辅助电感差分权重
    EEPROM_FLOAT(ring.profile.adc_c_l, 41, 0.60f), // 圆环阶段分母补偿权重
    EEPROM_FLOAT(ring.profile.kp_Err, 42, 5.50f),  // 圆环阶段方向环比例系数
    EEPROM_FLOAT(ring.profile.kd_Err, 43, 9.0f),   // 圆环阶段方向环微分系数
    EEPROM_FLOAT(ring.profile.kp2_Err, 44, 0.01f), // 圆环阶段方向环非线性增强系数
    /* --- RING DRIVE 子页面 --- */
    EEPROM_FLOAT(ring.profile.kp_Angle, 45, 1.40f),        // 圆环阶段角速度内环比例系数
    EEPROM_FLOAT(ring.profile.kd_Angle, 46, 1.52f),        // 圆环阶段角速度内环微分系数
    EEPROM_FLOAT(ring.profile.diff_inner_gain, 47, 0.60f), // 圆环阶段内轮减速增益
    EEPROM_FLOAT(ring.profile.diff_outer_gain, 48, 0.50f), // 圆环阶段外轮增速增益
    /* --- CYLINDER 页面 --- */
    EEPROM_FLOAT(cylinder.encoder_target, 49, 300.0f),   // 圆桶编码器积分退出阈值
    EEPROM_INT(cylinder.ad_both_high_threshold, 50, 45), // 圆桶双路强信号识别阈值
    EEPROM_FLOAT(cylinder.adc_a_1, 51, 1.00f),           // 圆桶专用横向主差分权重
    EEPROM_FLOAT(cylinder.adc_b_1, 52, 1.20f),           // 圆桶专用竖向差分权重
    EEPROM_FLOAT(cylinder.adc_c_l, 53, 0.60f),           // 圆桶专用分母补偿权重
    EEPROM_INT(cylinder.exit_slow_speed, 54, 60),        // 圆桶确认后阶梯减速的最低目标速度
    EEPROM_FLOAT(cylinder.kp_Err, 55, 1.0f),             // 圆桶专用方向环比例系数
    EEPROM_FLOAT(cylinder.kd_Err, 56, 1.0f),             // 圆桶专用方向环微分系数
    /* --- WALL 页面 --- */
    EEPROM_INT(wall.slow_speed, 57, 75),           // 墙面阶段降速目标值
    EEPROM_INT(wall.slow_time, 58, 150),           // 墙面阶段降速持续时间，×2ms
    EEPROM_INT(wall.timing_count, 59, 500),        // 墙面阶段下墙计时，×2ms
    EEPROM_FLOAT(wall.encoder_target, 60, 250.0f), // 墙面退出编码器积分阈值
    /* --- FLY 页面（飞坡参数 + 停止等待参数） --- */
    EEPROM_INT(fly.seesaw_mode, 61, 1),              // 飞坡模式选择：0-飞坡，1-停止等待
    EEPROM_INT(fly.fly_speed, 62, 15),               // 飞坡LOW阶段目标速度
    EEPROM_INT(fly.fly_detect_count, 63, 5),         // 飞坡入口弱磁确认次数
    EEPROM_INT(fly.fly_recover_speed, 64, 10),       // 飞坡COOLDOWN恢复速度
    EEPROM_FLOAT(fly.fly_release_step, 65, 0.2f),    // 飞坡COOLDOWN调节步长
    EEPROM_INT(fly.fly_land_confirm_count, 66, 10),  // 飞坡落地回升连续确认次数
    EEPROM_INT(fly.seesaw_speed, 67, 15),            // 停止等待模式CREEP阶段目标速度
    EEPROM_INT(fly.seesaw_detect_count, 68, 5),      // 跷跷板入口命中次数
    EEPROM_INT(fly.seesaw_wait_count, 69, 10),       // 停止等待模式停车等待时间，×2ms
    EEPROM_FLOAT(fly.seesaw_creep_cm, 70, 20.0f),    // 停止等待模式前挪距离
    EEPROM_FLOAT(fly.seesaw_release_step, 71, 0.2f), // 停止等待COOLDOWN调节步长
    /* --- CROSS 页面 --- */
    EEPROM_FLOAT(cross.encoder_target, 72, 300.0f), // 双十字退出编码器积分阈值
    EEPROM_FLOAT(cross.adc_a_1, 73, 1.0f),          // 双十字专用横向主差分权重
    EEPROM_FLOAT(cross.adc_b_1, 74, 1.20f),         // 双十字专用竖向差分权重
    EEPROM_FLOAT(cross.adc_c_l, 75, 0.60f),         // 双十字专用分母补偿权重
    EEPROM_FLOAT(cross.kp_Err, 76, 5.60),           // 双十字专用方向环比例系数
    EEPROM_FLOAT(cross.kd_Err, 77, 9.0f),           // 双十字专用方向环微分系数
    EEPROM_FLOAT(cross.kp2_Err, 78, 0.020f),        // 双十字专用方向环非线性增强系数
    /* --- CROSSS 页面 --- */
    EEPROM_FLOAT(cross_single.encoder_target, 79, 300.0f), // 单十字退出编码器积分阈值
    EEPROM_FLOAT(cross_single.adc_a_1, 80, 1.0f),          // 单十字专用横向主差分权重
    EEPROM_FLOAT(cross_single.adc_b_1, 81, 1.20f),         // 单十字专用竖向差分权重
    EEPROM_FLOAT(cross_single.adc_c_l, 82, 0.60f),         // 单十字专用分母补偿权重
    EEPROM_FLOAT(cross_single.kp_Err, 83, 5.55f),          // 单十字专用方向环比例系数
    EEPROM_FLOAT(cross_single.kd_Err, 84, 12.0f),          // 单十字专用方向环微分系数
    EEPROM_FLOAT(cross_single.kp2_Err, 85, 0.020f)};       // 单十字专用方向环非线性增强系数

#define EEPROM_CONFIG_ITEM_COUNT \
    ((uint8)(sizeof(eeprom_config_items) / sizeof(eeprom_config_items[0])))

static void eeprom_load_defaults(void);
static void eeprom_read_config(void);
static void eeprom_write_config(void);
static void eeprom_store_int(int32 input, uint8 value_bit);
static int32 eeprom_load_int(uint8 value_bit);
static void eeprom_store_float(float input, uint8 value_bit);
static float eeprom_load_float(uint8 value_bit);
static uint8 eeprom_float_is_abnormal(float value);

/**
 * @brief 判断浮点值是否为 NaN/Inf。
 * @param value 待检查浮点值。
 * @return uint8 1-NaN或Inf，0-有限值。
 *
 * C89 无 isnan/isfinite，按 IEEE754 位型判断：阶码 8 位全 1 即非有限值。
 * Flash 脏数据（写中途掉电的 0xFF、布局错位）解出的 NaN 一旦进控制环
 * 会污染全部 PID 输出，必须在加载入口拦下。
 */
static uint8 eeprom_float_is_abnormal(float value)
{
    union
    {
        float f;
        uint32 u;
    } conv;

    conv.f = value;
    return (uint8)(((conv.u >> 23) & 0xFFu) == 0xFFu);
}

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
    float value;

    for (i = 0; i < EEPROM_CONFIG_ITEM_COUNT; i++)
    {
        item = &eeprom_config_items[i];
        if (item->type == EEPROM_VALUE_FLOAT)
        {
            value = eeprom_load_float(item->slot);
            /* NaN/Inf 脏数据回落默认值，避免污染控制链。 */
            if (eeprom_float_is_abnormal(value) != 0)
                value = item->default_float;
            *((float *)item->target) = value;
        }
        else
        {
            *((int16 *)item->target) = (int16)eeprom_load_int(item->slot);
        }
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
    extern_iap_write_buff(EEPROM_CONFIG_SECTOR_ADDR, date_buff, sizeof(date_buff));
}

/**
 * @brief EEPROM 初始化主函数
 * @details
 * 1. 初始化 IAP 模块。
 * 2. 读取配置起始扇区（EEPROM_CONFIG_SECTOR_ADDR）的全部数据到内存缓冲区。
 * 3. 检查缓冲区最末槽位的标志位 `eeprom_init_time`。
 *    - 若不为 1，说明是首次上电、Flash 被擦除或上次写中途掉电，此时加载默认参数并写入 Flash。
 *    - 若为 1，说明 Flash 中有有效配置，直接从 Flash 读取参数到 `app`（写入顺序保证标志最后落盘）。
 */
void eeprom_init(void)
{
    /* 初始化 IAP (In-Application Programming) 模块 */
    iap_init();
    /* 从配置起始扇区读取完整配置到缓冲区。 */
    iap_read_buff(EEPROM_CONFIG_SECTOR_ADDR, date_buff, sizeof(date_buff));

    /* 预加载默认值到内存结构体（防止读取失败时无初值） */
    eeprom_load_defaults();

    /* 检查缓冲区最末槽位的标志位，判断是否为有效配置。 */
    eeprom_init_time = (uint8)eeprom_load_int(EEPROM_INIT_FLAG_SLOT);

    if (eeprom_init_time != 1)
    {
        /* 首次上电、Flash被擦除或上次写中途掉电：写入全套默认值，标志随整包最后写入。 */
        eeprom_init_time = 1;
        eeprom_store_int(eeprom_init_time, EEPROM_INIT_FLAG_SLOT);
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
