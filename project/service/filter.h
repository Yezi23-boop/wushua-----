#ifndef __FILTER_H__
#define __FILTER_H__

/**
 * @brief 一阶低通滤波器结构体
 * @details 记录滤波器的历史状态，用于平滑处理传感器数据
 */
typedef struct
{
    float out_last; /**< 上一次滤波器的输出值 */
} LowPassFilter_t;

/**
 * @brief 一阶低通滤波器更新函数
 * @param filter 滤波器结构体指针
 * @param value 输入值的指针，计算结果将直接写回该地址
 * @param alpha 滤波系数 (0.0~1.0)，alpha 越小滤波效果越强，但延迟也越大
 */
void low_pass_filter_mt(LowPassFilter_t *filter, volatile float *value, float alpha);

#endif /* __FILTER_H__ */
