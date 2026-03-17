#ifndef __ADC_H__
#define __ADC_H__

#include "zf_common_typedef.h"

/**
 * @brief 传感器数量
 * @details 对应四路电感传感器
 */
#define NUM 4

/* --- 全局变量声明 --- */

/**
 * @brief 四路归一化后的电感值 (0~100)
 */
extern uint16 ad1;
extern uint16 ad2;
extern uint16 ad3;
extern uint16 ad4;

/**
 * @brief 计算出的赛道位置偏差值
 */
extern volatile float Err;

/**
 * @brief 电感数据的最大值记录 (用于自动标定)
 */
extern volatile uint16 MA[NUM];

/**
 * @brief 电感数据的最小值记录 (用于自动标定)
 */
extern volatile uint16 MI[NUM];

/**
 * @brief 电感数据的实时原始采样值
 */
extern volatile uint16 RAW[NUM];

/* --- 函数声明 --- */

/**
 * @brief 设置 ADC 测量是否使能（通常在标定时使用）
 */
void adc_measure_set_enable(uint8 enable);

/**
 * @brief 重置电感标定记录的最大最小值
 */
void adc_measure_reset(void);

/**
 * @brief 自动扫描并更新电感数据的最大最小值
 */
void scan_track_max_value(void);

/**
 * @brief 执行一次完整的电感读取、滤波、归一化及偏差计算流程
 */
void read_AD(void);

#endif /* __ADC_H__ */
