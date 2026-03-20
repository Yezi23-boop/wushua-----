#ifndef __EEPROM_H
#define __EEPROM_H

#include "zf_common_typedef.h"

/**
 * @brief 启动相关配置结构体
 */
typedef struct
{
    int16 start_flag;   /**< 启动标志位：1-启动运行，0-停止待机 */
    int16 circle_flags; /**< 圆环方向标志：1-强制左环，-1-强制右环，0-自动识别 */
    float fuya_xili;    /**< 负压风扇吸力设定值（单位：占空比或特定数值） */
} AppStartConfig;

/**
 * @brief 速度环 PID 相关配置结构体
 * @details 主要用于电感差比和转向控制
 */
typedef struct
{
    float kp_Err;       /**< 转向误差比例系数 Kp（响应电感偏差的力度） */
    float kd_Err;       /**< 转向误差微分系数 Kd（抑制电感偏差变化的速率） */
    float speed_run;    /**< 赛道基础运行速度（cm/s 或编码器脉冲数） */
    float limiting_Err; /**< 转向输出限幅值（防止舵机/电机过载） */
    float kp2_Err;      /**< 二次项系数（用于处理大角度弯道的非线性增强） */
} AppSpeedConfig;

/**
 * @brief 角度环/姿态 PID 相关配置结构体
 * @details 主要用于陀螺仪姿态维持和转向
 */
typedef struct
{
    float kp_Angle;       /**< 角度环比例系数 Kp（响应角度偏差的力度） */
    float kd_Angle;       /**< 角度环微分系数 Kd（抑制角度变化的速率） */
    float limiting_Angle; /**< 角度环输出限幅值（限制最大转向角） */
    float A_1;            /**< 备用参数 A_1（可用于特殊赛道元素的调试） */
    float B_1;            /**< 备用参数 B_1（可用于特殊赛道元素的调试） */
    float C_l;            /**< 备用参数 C_l（可用于特殊赛道元素的调试） */
} AppAngleConfig;

/**
 * @brief 圆环元素识别与控制相关配置
 */
typedef struct
{
    float ring_encoder;          /**< 入环判定阈值：编码器积分距离达到此值确认入环 */
    float pre_ring_Gyro_set;     /**< 预入环姿态设定值（入环前的打角力度） */
    float in_ring_Gyroz;         /**< 环内巡航角速度设定值（维持圆周运动的角速度） */
    float pre_out_ring_Gyro_set; /**< 预出环姿态设定值（出环前的打角力度） */
    float pre_out_ring_Gyroz;    /**< 出环判定角速度阈值 */
    float pre_out_ring_encoder;  /**< 出环判定阈值：编码器积分距离达到此值确认出环 */
} AppRingConfig;

/**
 * @brief 飞坡/特殊元素控制配置
 */
typedef struct
{
    int count_fly_speed;   /**< 飞坡状态下的目标速度（通常为慢速以保安全） */
    int count_fly_time_1;  /**< 飞坡检测确认时间（连续多少次检测到特征才触发） */
    int count_fly_time_2;  /**< 飞坡状态持续时间（触发后保持该状态的时长，单位：10ms） */
    int count_fly_angle;   /**< 飞坡状态下的强制锁死舵机角度（0为不锁死） */
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
    AppAngleConfig angle; /**< 角度与姿态 PID 配置 */
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
