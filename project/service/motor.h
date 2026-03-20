#ifndef __MOTOR_H__
#define __MOTOR_H__

#include "zf_common_headfile.h"

/**
 * @brief 电机控制与外设初始化
 * @details 初始化 PWM 引脚、方向引脚以及相关的 GPIO 状态
 */
void motor_Init(void);

/**
 * @brief 电机占空比输出函数
 * @param lpwm 左电机 PWM 占空比（正值前进，负值后退）
 * @param rpwm 右电机 PWM 占空比（正值前进，负值后退）
 */
void motor_output(int32 lpwm, int32 rpwm);

/**
 * @brief 丢线检测保护函数
 * @details 当所有电感传感器均未检测到信号时，触发紧急停车
 */
void lost_lines(void);

/**
 * @brief 电压监测函数
 * @details 实时监测电池电压，防止锂电池过放电
 */
void dianya_jiance(void);

/**
 * @brief 速度转占空比的前馈查表函数
 * @param speed 目标物理速度
 * @return 对应的 PWM 基础占空比
 */
int32 motor_speed_to_duty(float speed);

/* --- 全局变量声明 --- */
extern volatile uint8 stop;   /**< 停车标志位：1 表示停止，0 表示运行 */
extern volatile float dianya; /**< 实时监测到的电池电压值 */

#endif /* __MOTOR_H__ */
