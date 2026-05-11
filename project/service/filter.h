#ifndef __FILTER_H__
#define __FILTER_H__

#include "zf_common_typedef.h"

/**
 * @brief 一阶低通滤波器结构体
 * @details 记录滤波器的历史状态，用于平滑处理传感器数据
 */
typedef struct
{
    float out_last; /**< 上一次滤波器的输出值 */
} LowPassFilter_t;

/**
 * @brief 浮点去极值平均滤波状态
 * @details
 * 维护固定 5 点窗口；当有效样本数不少于 3 时，先去掉 1 个最大值和 1 个最小值，
 * 再对剩余样本求平均。窗口未填满时按当前有效样本数工作。
 */
typedef struct
{
    float sample_hist[5];   /**< 5 点历史窗口 */
    uint8 write_index;      /**< 下一个写入位置 */
    uint8 valid_count;      /**< 当前有效样本数，最大为 5 */
} TrimmedMeanFilterFloatState;

/**
 * @brief 一阶低通滤波器更新函数
 * @param filter 滤波器结构体指针
 * @param value 输入值的指针，计算结果将直接写回该地址
 * @param alpha 滤波系数 (0.0~1.0)，alpha 越小滤波效果越强，但延迟也越大
 */
void low_pass_filter_mt(LowPassFilter_t *filter, volatile float *value, float alpha);

/**
 * @brief 复位浮点去极值平均滤波状态
 * @param state 滤波状态
 */
void TrimmedMeanFilterFloatReset(TrimmedMeanFilterFloatState *state);

/**
 * @brief 浮点去极值平均滤波更新
 * @param state 滤波状态
 * @param sample 当前输入样本
 * @return float 去极值平均后的输出
 */
float TrimmedMeanFilterFloatUpdate(TrimmedMeanFilterFloatState *state, float sample);

#endif /* __FILTER_H__ */
