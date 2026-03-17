#ifndef __EEPROM_H
#define __EEPROM_H

#include "zf_common_typedef.h"

/**
 * @brief 启动相关配置结构体
 */
typedef struct
{
    int16 start_flag;   /**< 启动标志：1-启动，0-停止 */
    int16 circle_flags; /**< 圆环方向标志：1-左环，-1-右环，0-自动 */
    float fuya_xili;    /**< 负压风扇吸力设定值 */
} AppStartConfig;

/**
 * @brief 速度环 PID 相关配置结构体
 */
typedef struct
{
    float kp_Err;       /**< 转向误差比例系数 Kp */
    float kd_Err;       /**< 转向误差微分系数 Kd */
    float speed_run;    /**< 赛道运行基础速度 */
    float limiting_Err; /**< 转向输出限幅值 */
    float kp2_Err;      /**< 增强项系数（非线性或陀螺仪关联） */
} AppSpeedConfig;

/**
 * @brief 角度环/姿态 PID 相关配置结构体
 */
typedef struct
{
    float kp_Angle;       /**< 角度环比例系数 Kp */
    float kd_Angle;       /**< 角度环微分系数 Kd */
    float limiting_Angle; /**< 角度环输出限幅值 */
    float A_1;            /**< 通用参数 A_1（用于不同赛道元素适配） */
    float B_1;            /**< 通用参数 B_1 */
    float C_l;            /**< 通用参数 C_l */
} AppAngleConfig;

/**
 * @brief 圆环元素识别与控制相关配置
 */
typedef struct
{
    float ring_encoder;          /**< 触发圆环判定的编码器距离阈值 */
    float pre_ring_Gyro_set;     /**< 预入环姿态设定值 */
    float in_ring_Gyroz;         /**< 入环后维持的角速度目标 */
    float pre_out_ring_Gyro_set; /**< 预出环姿态设定值 */
    float pre_out_ring_Gyroz;    /**< 预出环维持角速度 */
    float pre_out_ring_encoder;  /**< 出环判定编码器阈值 */
} AppRingConfig;

/**
 * @brief 飞坡/特殊元素控制配置
 */
typedef struct
{
    int count_fly_speed;   /**< 飞坡时的目标速度 */
    int count_fly_time_1;  /**< 飞坡检测延迟/持续时间 1 */
    int count_fly_time_2;  /**< 飞坡检测延迟/持续时间 2 */
    int count_fly_angle;   /**< 飞坡时的打角锁定值 */
    int16 fly_ramp_enable; /**< 飞坡模式开关：1-开启，0-关闭 */
} AppFlyConfig;

/**
 * @brief 应用程序全局配置聚合结构体
 */
typedef struct
{
    AppStartConfig start; /**< 启动配置 */
    AppSpeedConfig speed; /**< 速度配置 */
    AppAngleConfig angle; /**< 角度配置 */
    AppRingConfig ring;   /**< 圆环配置 */
    AppFlyConfig fly;     /**< 飞坡配置 */
} AppConfig;

/* --- 全局变量声明 --- */
extern uint8 date_buff[200];   /**< EEPROM 数据缓冲区 */
extern AppConfig g_app_config; /**< 全局配置对象 */

/**
 * @brief 初始化 EEPROM 管理器
 * @details 从 Flash 读取配置数据，若为首次运行则加载默认值
 */
void eeprom_init(void);

/**
 * @brief 将当前内存中的配置保存到 EEPROM
 */
void eeprom_flash(void);

#endif /* __EEPROM_H */
