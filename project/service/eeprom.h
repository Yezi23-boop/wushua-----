#ifndef __EEPROM_H
#define __EEPROM_H

#include "zf_common_typedef.h"

/**
 * @brief 启动相关配置结构体
 */
typedef struct
{
    int16 start_flag;        /**< 启动标志位：1-启动运行，0-停止待机 */
    int16 circle_flags;      /**< 圆环识别开关：1-开启，0-关闭 */
    int16 track_mode;        /**< 赛道元素模式：0-左圆环到圆筒循环，其余模式预留 */
    float fuya_xili;         /**< 平地负压百分比，范围 0~100 */
    float fuya_wall_percent; /**< 墙面负压百分比，范围 0~100 */
} AppStartConfig;

/**
 * @brief 速度环 PID 相关配置结构体
 * @details 主要用于电感差比和转向控制
 */
typedef struct
{
    float kp_Err;        /**< 转向误差比例系数 Kp（响应电感偏差的力度） */
    float kd_Err;        /**< 转向误差微分系数 Kd（抑制电感偏差变化的速率） */
    float gyro_damp_Err; /**< 转向环陀螺仪阻尼系数（抑制高速摆振） */
    float speed_run;     /**< 赛道基础运行速度（cm/s 或编码器脉冲数） */
    float limiting_Err;  /**< 转向输出限幅值（防止舵机/电机过载） */
    float kp2_Err;       /**< 二次项系数（用于处理大角度弯道的非线性增强） */
} AppSpeedConfig;

/**
 * @brief 电感偏差解算参数结构体
 * @details 主要用于四路电感差比和归一化解算
 */
typedef struct
{
    float kp_Angle;            /**< 角速度内环比例系数 Kp（跟踪转向目标角速度） */
    float kd_Angle;            /**< 角速度内环微分系数 Kd（抑制角速度过冲） */
    float gyro_feedback_scale; /**< 角速度反馈缩放系数 N（匹配 gyro_z 与目标角速度量级） */
    float limiting_Angle;      /**< 角速度内环输出限幅（差速目标限幅） */
    float A_1;                 /**< 主亮度权重，用于横向主差分归一化 */
    float B_1;                 /**< 竖向差分权重，用于斜入/斜出姿态修正 */
    float C_l;                 /**< 分母补偿权重，用于弱信号时抑制偏差放大 */
} AppAngleConfig;

/**
 * @brief 圆环元素识别与控制相关配置
 */
typedef struct
{
    float ring_entry_encoder;       /**< ring阶段编码器积分阈值，达到后进入pre_ring */
    float pre_ring_Gyro_target;     /**< pre_ring阶段固定目标角速度 */
    float pre_ring_Gyroz;           /**< pre_ring阶段累计转角阈值，达到后进入in_ring */
    float in_ring_Gyroz;            /**< in_ring阶段累计转角阈值，达到后进入pre_out_ring */
    float pre_out_ring_Gyro_target; /**< pre_out_ring阶段固定目标角速度 */
    float pre_out_ring_Gyroz;       /**< pre_out_ring阶段累计转角阈值，达到后进入out_ring */
} AppRingConfig;

/**
 * @brief 飞坡/特殊元素控制配置
 */
typedef struct
{
    int count_fly_speed;   /**< 飞坡状态下的目标速度（通常为慢速以保安全） */
    int count_fly_time_1;  /**< 飞坡检测确认时间（按 5ms 主环累计的触发次数） */
    int count_fly_time_2;  /**< 飞坡状态持续时间（触发后保持该状态的时长，单位：5ms） */
    int16 fly_ramp_enable; /**< 飞坡模式功能开关：1-开启检测，0-关闭检测 */
} AppFlyConfig;

/**
 * @brief 应用程序全局配置聚合结构体
 * @details 包含所有子模块的配置参数，整个结构体会被保存到 EEPROM
 */
typedef struct
{
    AppStartConfig start; /**< 启动与基础配置 */
    AppSpeedConfig speed; /**< 速度与转向 PID 配置 */
    AppAngleConfig angle; /**< 电感偏差解算配置 */
    AppRingConfig ring;   /**< 圆环处理策略配置 */
    AppFlyConfig fly;     /**< 飞坡处理策略配置 */
} AppConfig;

/* --- 全局变量声明 --- */
extern uint8 date_buff[200]; /**< EEPROM 数据读写缓冲区（200字节） */
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
