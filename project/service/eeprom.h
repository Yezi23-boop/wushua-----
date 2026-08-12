#ifndef __EEPROM_H
#define __EEPROM_H

#include "zf_common_typedef.h"
#include "a_run_track_element.h"

/**
 * @brief 启动相关配置结构体
 */
typedef struct
{
    int16 start_flag;                            /**< 启动标志位：1-启动运行，0-停止待机 */
    int16 element_enable;                        /**< 整体赛道元素识别开关：1-开启，0-关闭 */
    float fuya_xili;                             /**< 平地负压百分比，范围 0~100 */
    float encoder_stop_distance_cm;              /**< 上电累计里程达到该值后停车，单位 cm */
    int element_seq[TRACK_ELEMENT_SEQUENCE_MAX]; /**< 元素序列槽位：0空、1左环、2右环、3圆桶、4墙面、5跷跷板、6双十字；其他值运行期跳过。 */
} AppStartConfig;

/**
 * @brief 转向差速环 PID 相关配置结构体
 * @details 基于电感偏差的转向差速控制，包含基础运行速度设定
 */
typedef struct
{
    float kp_Err;          /**< 转向误差比例系数 Kp（响应电感偏差的力度） */
    float kd_Err;          /**< 转向误差微分系数 Kd（抑制电感偏差变化的速率） */
    float gyro_damp_Err;   /**< 转向环陀螺仪阻尼系数（抑制高速摆振） */
    float speed_run;       /**< 赛道基础运行速度（cm/s 或编码器脉冲数） */
    float limiting_Err;    /**< 转向输出限幅值（防止舵机/电机过载） */
    float kp2_Err;         /**< 二次项系数（用于处理大角度弯道的非线性增强） */
    int16 diff_enable;     /**< 非线性内外轮差速开关：1-开启，0-使用线性差速 */
    float diff_inner_gain; /**< 差速分配内轮减速增益 */
    float diff_outer_gain; /**< 差速分配外轮增速增益 */
} AppSpeedConfig;

/**
 * @brief 电感偏差解算参数结构体
 * @details 主要用于四路电感差比和归一化解算
 */
typedef struct
{
    float kp_Angle;             /**< 角速度内环比例系数 Kp（跟踪转向目标角速度） */
    float kd_Angle;             /**< 角速度内环微分系数 Kd（抑制角速度过冲） */
    float gyro_feedback_scale;  /**< 角速度反馈缩放系数 N（匹配 gyro_z 与目标角速度量级） */
    float limiting_Angle;       /**< 角速度内环输出限幅（差速目标限幅） */
    float A_1;                  /**< 主亮度权重，用于横向主差分归一化 */
    float B_1;                  /**< 竖向差分权重，用于斜入/斜出姿态修正 */
    float C_l;                  /**< 分母补偿权重，用于弱信号时抑制偏差放大 */
    float strong_signal_sum;    /**< 强信号姿态锁定阈值：四路电感和超过该值时转向外环锁定当前航向 */
    float strong_correct_angle; /**< 强信号区反向修正角速度，带符号，方向与冻结Err相反，负值反向 */
    int16 strong_signal_enable; /**< 强信号姿态锁定总开关：1-开启（四路和超阈锁定航向），0-关闭 */
} AppAngleConfig;

/**
 * @brief 电感偏置圆环单套参数。
 */
typedef struct
{
    float bias_entry_gain;        /**< 进环阶段同侧两路电感放大倍数 */
    float bias_exit_gain;         /**< 出环阶段对侧两路电感放大倍数 */
    float entry_straight_encoder; /**< 识别后零角速度直走距离（cm） */
    float bias_entry_yaw;         /**< 结束进环偏置的累计转角阈值（度） */
    float bias_entry_encoder;     /**< 结束进环偏置的编码器距离阈值（cm） */
    float bias_finish_encoder;    /**< 圆环完成的编码器距离阈值（cm） */
    float bias_finish_yaw;        /**< 出环满圈角度积分阈值（度），与里程双条件确认 */
    float target_speed;           /**< 圆环进环、环内和出环阶段目标速度 */
    float adc_a_1;                /**< 圆环阶段横向主差分权重 */
    float adc_b_1;                /**< 圆环阶段辅助电感差分权重 */
    float adc_c_l;                /**< 圆环阶段分母补偿权重 */
    float kp_Err;                 /**< 圆环阶段方向环比例系数 */
    float kd_Err;                 /**< 圆环阶段方向环微分系数 */
    float kp2_Err;                /**< 圆环阶段方向环非线性增强系数 */
    float kp_Angle;               /**< 圆环阶段角速度内环比例系数 */
    float kd_Angle;               /**< 圆环阶段角速度内环微分系数 */
    float diff_inner_gain;        /**< 圆环阶段内轮减速增益 */
    float diff_outer_gain;        /**< 圆环阶段外轮增速增益 */
} AppRingProfileConfig;

/**
 * @brief 圆环元素识别与控制相关配置。
 */
typedef struct
{
    float gain_speed_slope;       /**< 目标速度每增加1时进环增益的增加量 */
    AppRingProfileConfig profile; /**< 可独立保存和调节的电感偏置圆环参数 */
} AppRingConfig;

/**
 * @brief 飞坡/跷跷板控制配置
 */
typedef struct
{
    /* 飞坡模式专用 */
    int fly_speed;              /**< LOW 阶段目标速度 */
    int fly_detect_count;       /**< IDLE 入口弱磁确认次数 */
    int fly_recover_speed;      /**< 飞坡 COOLDOWN 恢复速度 */
    int fly_land_confirm_count; /**< 落地回升连续确认次数 */
    float fly_release_step;     /**< 飞坡 COOLDOWN 步长 */
    /* 停止等待模式专用 */
    int seesaw_detect_count;   /**< IDLE 入口命中次数 */
    int seesaw_wait_count;     /**< 停车等待时间（×2ms） */
    int seesaw_speed;          /**< CREEP 阶段目标速度 */
    float seesaw_creep_cm;     /**< 前挪距离（cm） */
    float seesaw_release_step; /**< 停止等待 COOLDOWN 步长 */
    /* 共用 */
    int16 seesaw_mode; /**< 0=飞坡，1=停止等待 */
} AppFlyConfig;

/**
 * @brief 圆桶元素识别与控制相关配置
 */
typedef struct
{
    float encoder_target;       /**< 圆桶编码器积分阈值，后续切换到里程退出时使用。 */
    int ad_both_high_threshold; /**< 圆桶双路强信号阈值，后续替代固定宏调参。 */
    float adc_a_1;              /**< 圆桶专用横向主差分权重。 */
    float adc_b_1;              /**< 圆桶专用竖向差分权重。 */
    float adc_c_l;              /**< 圆桶专用分母补偿权重。 */
    float kp_Err;               /**< 圆桶专用方向环比例系数，后续运行期切换 PID 时使用。 */
    float kd_Err;               /**< 圆桶专用方向环微分系数，后续运行期切换 PID 时使用。 */
    int exit_slow_speed;        /**< 圆桶确认后阶梯减速的最低目标速度。 */
} AppCylinderConfig;

/**
 * @brief 墙面元素控制配置
 */
typedef struct
{
    int slow_speed;       /**< 墙面阶段降速目标值。 */
    int slow_time;        /**< 墙面阶段降速持续时间，单位为 2ms 主控制周期。 */
    int timing_count;     /**< 墙面阶段下墙计时，单位为 2ms 主控制周期。 */
    float encoder_target; /**< 墙面退出编码器积分阈值。 */
} AppWallConfig;

/**
 * @brief 双十字元素控制配置
 */
typedef struct
{
    float encoder_target; /**< 双十字退出编码器积分阈值 */
    float adc_a_1;        /**< 双十字专用横向主差分权重。 */
    float adc_b_1;        /**< 双十字专用竖向差分权重。 */
    float adc_c_l;        /**< 双十字专用分母补偿权重。 */
    float kp_Err;         /**< 双十字专用方向环比例系数 */
    float kd_Err;         /**< 双十字专用方向环微分系数 */
    float kp2_Err;        /**< 双十字专用方向环非线性增强系数 */
} AppCrossConfig;

/**
 * @brief 单十字元素控制配置
 */
typedef struct
{
    float encoder_target; /**< 单十字退出编码器积分阈值 */
    float adc_a_1;        /**< 单十字专用横向主差分权重。 */
    float adc_b_1;        /**< 单十字专用竖向差分权重。 */
    float adc_c_l;        /**< 单十字专用分母补偿权重。 */
    float kp_Err;         /**< 单十字专用方向环比例系数 */
    float kd_Err;         /**< 单十字专用方向环微分系数 */
    float kp2_Err;        /**< 单十字专用方向环非线性增强系数 */
} AppCrossSingleConfig;

/**
 * @brief 应用程序全局配置聚合结构体
 * @details 包含所有子模块的配置参数，整个结构体会被保存到 EEPROM
 */
typedef struct
{
    AppStartConfig start;              /**< 启动与基础配置 */
    AppSpeedConfig speed;              /**< 速度与转向 PID 配置 */
    AppAngleConfig angle;              /**< 电感偏差解算配置 */
    AppRingConfig ring;                /**< 圆环处理策略配置 */
    AppFlyConfig fly;                  /**< 飞坡处理策略配置 */
    AppCylinderConfig cylinder;        /**< 圆桶处理策略配置 */
    AppWallConfig wall;                /**< 墙面处理策略配置 */
    AppCrossConfig cross;              /**< 双十字处理策略配置 */
    AppCrossSingleConfig cross_single; /**< 单十字处理策略配置 */
} AppConfig;

/* --- 全局变量声明 --- */
extern uint8 date_buff[408]; /**< EEPROM 数据读写缓冲区，覆盖到逻辑槽位 101。 */
extern AppConfig app;        /**< 全局配置对象实例，运行时参数均从此读取 */

/**
 * @brief 初始化 EEPROM 管理器
 * @details
 * 1. 初始化 IAP 模块
 * 2. 从 Flash 读取配置数据到内存
 * 3. 校验数据有效性，若无效则加载默认值并写入 Flash
 */
void eeprom_init(void);

/**
 * @brief 将当前内存中的配置保存到 EEPROM
 * @details 将 app 的内容序列化并写入 Flash，掉电不丢失
 */
void eeprom_flash(void);

#endif /* __EEPROM_H */
