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
 * @note 由于主要在 5ms 周期定时器中断 (TM0) 内被更新，并在主循环读取，故作 volatile 修饰。
 */
extern volatile uint16 ad1;
extern volatile uint16 ad2;
extern volatile uint16 ad3;
extern volatile uint16 ad4;

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
 *
 * @details 
 * 在开启标定使能的情况下，实时比对当前 RAW 采样值与历史最大/最小值，
 * 用于动态调整差比和算法的映射归一化区间，避免由于赛道反光率不同导致的识别失效。
 *
 * @note 必须周期性调用(如在 10ms 状态环内)。
 */
void scan_track_max_value(void);

/**
 * @brief 执行完整的电感数据采集与解算
 *
 * @details 
 * 包含多通道采样、选择排序、去极值均值滤波、归一化及四路电感差比和计算输出 Err。
 * 该函数处于高频控制链(2ms TM0 内)，任何修改都需避免引入阻塞。
 *
 * @note ISR 上下文，必须保证低耗时。
 */
void read_AD(void);

#endif /* __ADC_H__ */
