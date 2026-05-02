#ifndef __FILTER_H__
#define __FILTER_H__

#include "zf_common_typedef.h"

/*
 * 编码器符号纠错参数，单位均为“原始编码器计数”。
 * 若现场需要微调，可直接改这些宏，不影响主控制链路。
 */
#ifndef ENC_ZERO_DEADBAND
#define ENC_ZERO_DEADBAND 5
#endif

#ifndef ENC_SIGN_FIX_MIN
#define ENC_SIGN_FIX_MIN 8
#endif

#ifndef ENC_SIGN_MAG_TOL
#define ENC_SIGN_MAG_TOL 16
#endif

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
 * @brief 编码器符号纠错状态
 * @details 保留最近 3 次原始编码器计数，仅用于单点反号异常修正
 */
typedef struct
{
    int32 raw_hist[3];      /**< 最近 3 次原始编码器计数 */
    uint8 hist_valid_count; /**< 历史样本有效数量，满 3 后启用纠错 */
} EncoderSignFixState;

/**
 * @brief 编码器 3 点中值 + EMA(1/2) 滤波状态
 * @details 先做单点符号纠错，再做 3 点中值，最后做 EMA(1/2) 平滑
 */
typedef struct
{
    EncoderSignFixState sign_fix; /**< 单点反号纠错状态 */
    int32 median_hist[3];         /**< 中值滤波历史窗口（使用纠错后的原始计数） */
    uint8 median_valid_count;     /**< 中值窗口有效样本数 */
    int32 ema_out_raw;            /**< EMA 输出的原始计数值 */
    uint8 ema_valid;              /**< EMA 输出是否已初始化 */
} EncoderMedian3EmaFilterState;

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

/**
 * @brief 基于最近 3 个原始编码器值修正单点符号误判
 * @param raw_now 当前原始编码器计数
 * @param state 编码器符号纠错状态
 * @return 修正后的原始编码器计数
 */
int32 CorrectEncoderSignByHistory(int32 raw_now, EncoderSignFixState *state);

/**
 * @brief 编码器组合滤波：符号纠错 + 3 点中值 + EMA(1/2)
 * @param raw_now 当前原始编码器计数
 * @param state 编码器组合滤波状态
 * @return 组合滤波后的原始编码器计数
 */
int32 FilterEncoderCountMedian3EmaHalf(int32 raw_now, EncoderMedian3EmaFilterState *state);

#endif /* __FILTER_H__ */
