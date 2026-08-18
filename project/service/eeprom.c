#include "zf_common_headfile.h"

/* 数据缓冲区覆盖逻辑槽位0~99，每个槽位占4字节。 */
uint8 date_buff[400];
/* 参数存储起始扇区：一扇区 512 字节，date_buff 占 400 字节不跨扇区。
 * 换扇区后首次上电新扇区全 0xFF，初始化标志判为非首次，自动写入全套默认值，
 * 旧扇区里的现场参数不会携带过来，需重新调参保存。 */
#define EEPROM_CONFIG_SECTOR_ADDR 0x200u
/* 初始化标志槽位：固定在 99（字节396~399），是缓冲区最后一个槽位。
 * 标志随整包最后写入，读到标志有效即整包参数已写完；
 * 全部 float 参数加载时仍统一过 NaN 检测，防写中途掉电的半截脏数据进控制链。 */
#define EEPROM_INIT_FLAG_SLOT 99
/* EEPROM 初始化标志位，用于判断是否为首次上电（0-首次，1-非首次） */
static uint8 eeprom_init_time = 0;
/* 全局配置结构体实例，运行时所有的参数都从这里读取 */
AppConfig app;

/*
 * 槽位布局（按菜单显示顺序）：1~8 元素序列 E1~E8，9~13 START，14~19 CTRL，
 * 20~25 MODEL，26~42 小圆环 RING，43~59 大圆环 RING，
 * 60~67 CYLINDER，68~70 WALL，71~84 FLY，85~91 CROSS，
 * 92~98 CROSSS，99 初始化标志。整片重烧时按默认值全量写入，现场调参后需重新保存。
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
// #define TRACK_ELEMENT_NONE 0             /**< 空槽位，用于跳过或现场临时关闭某个序列位置。 */
// #define TRACK_ELEMENT_LEFT_RING 1        /**< 左圆环元素编号，接入序列表串行仲裁。 */
// #define TRACK_ELEMENT_RIGHT_RING 2       /**< 右圆环元素编号，复用圆环状态机并反向控制。 */
// #define TRACK_ELEMENT_LARGE_RING_LEFT 3  /**< 大圆环左元素编号，复用圆环状态机，参数取大圆环组。 */
// #define TRACK_ELEMENT_LARGE_RING_RIGHT 4 /**< 大圆环右元素编号，复用圆环状态机，参数取大圆环组。 */
// #define TRACK_ELEMENT_CYLINDER 5         /**< 圆桶元素编号。 */
// #define TRACK_ELEMENT_WALL 6             /**< 墙面元素编号。 */
// #define TRACK_ELEMENT_SEESAW 7           /**< 跷跷板元素编号，复用 a_run_fly 的弱磁/恢复状态机。 */
// #define TRACK_ELEMENT_DOUBLE_CROSS 8     /**< 双十字元素编号，电感和命中后编码器积分退出。 */
// #define TRACK_ELEMENT_SINGLE_CROSS 9     /**< 单十字元素编号，入口判定与双十字相同，仅序列区分。 */
static const EepromConfigItem eeprom_config_items[] = {
    /* --- 元素序列（ELEM/ELEM2 页面，放最前便于现场调整序列） --- */
    EEPROM_INT(start.element_seq[0], 1, TRACK_ELEMENT_SEESAW),          // 元素序列槽位0
    EEPROM_INT(start.element_seq[1], 2, TRACK_ELEMENT_LARGE_RING_LEFT), // 元素序列槽位1
    EEPROM_INT(start.element_seq[2], 3, TRACK_ELEMENT_CYLINDER),        // 元素序列槽位2
    EEPROM_INT(start.element_seq[3], 4, TRACK_ELEMENT_RIGHT_RING),      // 元素序列槽位3
    EEPROM_INT(start.element_seq[4], 5, TRACK_ELEMENT_WALL),            // 元素序列槽位4
    EEPROM_INT(start.element_seq[5], 6, TRACK_ELEMENT_NONE),            // 元素序列槽位5
    EEPROM_INT(start.element_seq[6], 7, TRACK_ELEMENT_NONE),            // 元素序列槽位6
    EEPROM_INT(start.element_seq[7], 8, TRACK_ELEMENT_NONE),            // 元素序列槽位7
    /* --- START 页面 --- */
    EEPROM_INT(start.start_flag, 9, 1),                         // 启动标志：1-运行，0-待机
    EEPROM_INT(start.element_enable, 10, 1),                    // 赛道元素识别总开关
    EEPROM_FLOAT(start.fuya_xili, 11, 90.0f),                   // 平地负压吸附百分比
    EEPROM_FLOAT(angle.gyro_feedback_scale, 12, 1.80f),         // 角速度反馈缩放，匹配gyro_z量级
    EEPROM_FLOAT(start.encoder_stop_distance_cm, 13, 11500.0f), // 上电累计里程达到后停车
    /* --- CTRL 页面 --- */
    EEPROM_FLOAT(speed.kp_Err, 14, 5.70f),        // 转向环比例系数Kp
    EEPROM_FLOAT(speed.kd_Err, 15, 9.0f),         // 转向环微分系数Kd
    EEPROM_FLOAT(speed.gyro_damp_Err, 16, 0.0f),  // 转向环陀螺仪阻尼，抑制高速摆振
    EEPROM_FLOAT(speed.speed_run, 17, 50.0f),     // 赛道基础运行速度
    EEPROM_FLOAT(speed.limiting_Err, 18, 800.0f), // 转向输出限幅
    EEPROM_FLOAT(speed.kp2_Err, 19, 0.015f),      // 转向环二次项非线性增强系数
    /* --- MODEL 页面 --- */
    EEPROM_FLOAT(angle.kp_Angle, 20, 1.21f),       // 角速度内环比例系数Kp
    EEPROM_FLOAT(angle.kd_Angle, 21, 1.16f),       // 角速度内环微分系数Kd
    EEPROM_FLOAT(angle.limiting_Angle, 22, 48.0f), // 角速度内环输出限幅
    EEPROM_FLOAT(angle.A_1, 23, 1.00f),            // 横向主差分权重
    EEPROM_FLOAT(angle.B_1, 24, 1.20f),            // 竖向差分权重，斜入/斜出姿态修正
    EEPROM_FLOAT(angle.C_l, 25, 0.60f),            // 分母补偿权重，弱信号时抑制偏差放大
    /* --- RING 页面（小圆环参数组） --- */
    EEPROM_FLOAT(ring.small_profile.entry_straight_encoder, 26, 5.0f), // 小圆环入口识别后零角速度直走距离
    /* 小圆环进环增益随目标速度的补偿斜率 */
    EEPROM_FLOAT(ring.small_profile.gain_speed_slope, 27, 0.09f),
    /* --- RING ENTRY 子页面 --- */
    EEPROM_FLOAT(ring.small_profile.bias_entry_gain, 28, 2.30f),       // 小圆环进环阶段同侧两路电感放大倍数
    EEPROM_FLOAT(ring.small_profile.bias_exit_gain, 29, 1.20f),        // 小圆环出环阶段对侧两路电感放大倍数
    EEPROM_FLOAT(ring.small_profile.bias_entry_yaw, 30, 40.0f),        // 小圆环结束进环偏置的累计转角阈值
    EEPROM_FLOAT(ring.small_profile.bias_entry_encoder, 31, 3.00f),    // 小圆环结束进环偏置的里程阈值
    EEPROM_FLOAT(ring.small_profile.bias_finish_encoder, 32, 100.00f), // 小圆环出环判定的里程阈值
    EEPROM_FLOAT(ring.small_profile.bias_finish_yaw, 33, 360.0f),      // 小圆环出环满圈角度积分阈值，与里程双条件确认
    EEPROM_FLOAT(ring.small_profile.target_speed, 34, 50.0f),          // 小圆环进环/环内/出环目标速度
    /* --- RING CTRL 子页面 --- */
    EEPROM_FLOAT(ring.small_profile.adc_a_1, 35, 1.0f),  // 小圆环阶段横向主差分权重
    EEPROM_FLOAT(ring.small_profile.adc_b_1, 36, 1.20f), // 小圆环阶段辅助电感差分权重
    EEPROM_FLOAT(ring.small_profile.adc_c_l, 37, 0.60f), // 小圆环阶段分母补偿权重
    EEPROM_FLOAT(ring.small_profile.kp_Err, 38, 5.50f),  // 小圆环阶段方向环比例系数
    EEPROM_FLOAT(ring.small_profile.kd_Err, 39, 9.0f),   // 小圆环阶段方向环微分系数
    EEPROM_FLOAT(ring.small_profile.kp2_Err, 40, 0.01f), // 小圆环阶段方向环非线性增强系数
    /* --- RING DRIVE 子页面 --- */
    EEPROM_FLOAT(ring.small_profile.kp_Angle, 41, 1.40f), // 小圆环阶段角速度内环比例系数
    EEPROM_FLOAT(ring.small_profile.kd_Angle, 42, 1.52f), // 小圆环阶段角速度内环微分系数
    /* --- LARGE RING 页面（大圆环独立参数组，槽位 43~59 紧跟小圆环，与菜单 RING 页顺序一致） --- */
    EEPROM_FLOAT(ring.large_profile.entry_straight_encoder, 43, 5.0f), // 大圆环入口识别后零角速度直走距离
    EEPROM_FLOAT(ring.large_profile.gain_speed_slope, 44, 0.09f),      // 大圆环进环增益随目标速度的补偿斜率
    /* --- LARGE RING ENTRY 子页面 --- */
    EEPROM_FLOAT(ring.large_profile.bias_entry_gain, 45, 1.80f),       // 大圆环进环阶段同侧两路电感放大倍数
    EEPROM_FLOAT(ring.large_profile.bias_exit_gain, 46, 1.20f),        // 大圆环出环阶段对侧两路电感放大倍数
    EEPROM_FLOAT(ring.large_profile.bias_entry_yaw, 47, 40.0f),        // 大圆环结束进环偏置的累计转角阈值
    EEPROM_FLOAT(ring.large_profile.bias_entry_encoder, 48, 3.00f),    // 大圆环结束进环偏置的里程阈值
    EEPROM_FLOAT(ring.large_profile.bias_finish_encoder, 49, 100.00f), // 大圆环出环判定的里程阈值
    EEPROM_FLOAT(ring.large_profile.bias_finish_yaw, 50, 360.0f),      // 大圆环出环满圈角度积分阈值，与里程双条件确认
    EEPROM_FLOAT(ring.large_profile.target_speed, 51, 50.0f),          // 大圆环进环/环内/出环目标速度
    /* --- LARGE RING CTRL 子页面 --- */
    EEPROM_FLOAT(ring.large_profile.adc_a_1, 52, 1.0f),  // 大圆环阶段横向主差分权重
    EEPROM_FLOAT(ring.large_profile.adc_b_1, 53, 1.20f), // 大圆环阶段辅助电感差分权重
    EEPROM_FLOAT(ring.large_profile.adc_c_l, 54, 0.60f), // 大圆环阶段分母补偿权重
    EEPROM_FLOAT(ring.large_profile.kp_Err, 55, 5.50f),  // 大圆环阶段方向环比例系数
    EEPROM_FLOAT(ring.large_profile.kd_Err, 56, 9.0f),   // 大圆环阶段方向环微分系数
    EEPROM_FLOAT(ring.large_profile.kp2_Err, 57, 0.01f), // 大圆环阶段方向环非线性增强系数
    /* --- LARGE RING DRIVE 子页面 --- */
    EEPROM_FLOAT(ring.large_profile.kp_Angle, 58, 1.40f), // 大圆环阶段角速度内环比例系数
    EEPROM_FLOAT(ring.large_profile.kd_Angle, 59, 1.52f), // 大圆环阶段角速度内环微分系数
    /* --- CYLINDER 页面 --- */
    EEPROM_FLOAT(cylinder.encoder_target, 60, 300.0f),   // 圆桶编码器积分退出阈值
    EEPROM_INT(cylinder.ad_both_high_threshold, 61, 45), // 圆桶双路强信号识别阈值
    EEPROM_FLOAT(cylinder.adc_a_1, 62, 1.00f),           // 圆桶专用横向主差分权重
    EEPROM_FLOAT(cylinder.adc_b_1, 63, 1.20f),           // 圆桶专用竖向差分权重
    EEPROM_FLOAT(cylinder.adc_c_l, 64, 0.60f),           // 圆桶专用分母补偿权重
    EEPROM_INT(cylinder.exit_slow_speed, 65, 50),        // 圆桶确认后阶梯减速的最低目标速度
    EEPROM_FLOAT(cylinder.kp_Err, 66, 1.0f),             // 圆桶专用方向环比例系数
    EEPROM_FLOAT(cylinder.kd_Err, 67, 1.0f),             // 圆桶专用方向环微分系数
    /* --- WALL 页面 --- */
    EEPROM_INT(wall.entry_speed, 68, 50),           // 墙面全程目标速度，低于 speed_run 减速、高于则加速
    EEPROM_INT(wall.timing_count, 69, 500),         // 墙面阶段下墙计时，×2ms
    EEPROM_FLOAT(wall.encoder_target, 70, 250.0f),  // 墙面退出编码器积分阈值
    /* --- FLY 页面（飞坡参数 + 停止等待参数 + 释放期居中权重） --- */
    EEPROM_INT(fly.seesaw_mode, 71, 1),              // 飞坡模式选择：0-飞坡，1-停止等待
    EEPROM_INT(fly.fly_speed, 72, 15),               // 飞坡LOW阶段目标速度
    EEPROM_INT(fly.fly_detect_count, 73, 5),         // 飞坡入口弱磁确认次数
    EEPROM_INT(fly.fly_recover_speed, 74, 10),       // 飞坡COOLDOWN恢复速度
    EEPROM_FLOAT(fly.fly_release_step, 75, 0.3f),    // 飞坡COOLDOWN调节步长
    EEPROM_INT(fly.fly_land_confirm_count, 76, 10),  // 飞坡落地回升连续确认次数
    EEPROM_INT(fly.seesaw_speed, 77, 15),            // 停止等待模式CREEP阶段目标速度
    EEPROM_INT(fly.seesaw_detect_count, 78, 3),      // 跷跷板入口命中次数
    EEPROM_INT(fly.seesaw_wait_count, 79, 15),       // 停止等待模式停车等待时间，×2ms
    EEPROM_FLOAT(fly.seesaw_creep_cm, 80, 25.0f),    // 停止等待模式前挪距离
    EEPROM_FLOAT(fly.seesaw_release_step, 81, 0.3f), // 停止等待COOLDOWN调节步长
    EEPROM_FLOAT(fly.center_a_1, 82, 1.00f),         // 释放期居中横向主差分权重
    EEPROM_FLOAT(fly.center_b_1, 83, 1.20f),         // 释放期居中竖向差分权重
    EEPROM_FLOAT(fly.center_c_l, 84, 0.60f),         // 释放期居中分母补偿权重
    /* --- CROSS 页面 --- */
    EEPROM_FLOAT(cross.encoder_target, 85, 300.0f), // 双十字退出编码器积分阈值
    EEPROM_FLOAT(cross.adc_a_1, 86, 1.0f),          // 双十字专用横向主差分权重
    EEPROM_FLOAT(cross.adc_b_1, 87, 1.20f),         // 双十字专用竖向差分权重
    EEPROM_FLOAT(cross.adc_c_l, 88, 0.60f),         // 双十字专用分母补偿权重
    EEPROM_FLOAT(cross.kp_Err, 89, 5.60),           // 双十字专用方向环比例系数
    EEPROM_FLOAT(cross.kd_Err, 90, 9.0f),           // 双十字专用方向环微分系数
    EEPROM_FLOAT(cross.kp2_Err, 91, 0.020f),        // 双十字专用方向环非线性增强系数
    /* --- CROSSS 页面 --- */
    EEPROM_FLOAT(cross_single.encoder_target, 92, 300.0f), // 单十字退出编码器积分阈值
    EEPROM_FLOAT(cross_single.adc_a_1, 93, 1.0f),         // 单十字专用横向主差分权重
    EEPROM_FLOAT(cross_single.adc_b_1, 94, 1.20f),        // 单十字专用竖向差分权重
    EEPROM_FLOAT(cross_single.adc_c_l, 95, 0.60f),        // 单十字专用分母补偿权重
    EEPROM_FLOAT(cross_single.kp_Err, 96, 5.55f),         // 单十字专用方向环比例系数
    EEPROM_FLOAT(cross_single.kd_Err, 97, 12.0f),         // 单十字专用方向环微分系数
    EEPROM_FLOAT(cross_single.kp2_Err, 98, 0.020f)};      // 单十字专用方向环非线性增强系数

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
 * 3. 检查缓冲区最末槽位 99 的初始化标志 `eeprom_init_time`。
 *    - 若不为 1，说明是首次上电、Flash 被擦除或上次写中途掉电，此时加载默认参数并写入 Flash。
 *    - 若为 1，说明 Flash 中有有效配置，直接从 Flash 读取参数到 `app`。
 *      标志位于缓冲区末尾，标志有效即整包参数完整；float 参数仍由 NaN 检测兜底。
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
