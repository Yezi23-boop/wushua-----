#include "zf_common_headfile.h"

/**
 * @brief 一阶低通滤波器实现
 * @details 公式：Output(n) = alpha * Input(n) + (1 - alpha) * Output(n-1)
 * @param filter 滤波器状态结构体
 * @param value 输入/输出数据指针
 * @param alpha 截止频率相关系数
 */
void low_pass_filter_mt(LowPassFilter_t *filter, volatile float *value, float alpha)
{
    float out;

    /*
     * 一阶低通滤波计算
     * alpha 接近 1 时，输出跟随输入较快；
     * alpha 接近 0 时，输出主要取决于历史值，平滑度高。
     */
    out = alpha * (*value) + (1.0f - alpha) * filter->out_last;

    /* 更新历史记录 */
    filter->out_last = out;

    /* 将滤波后的平滑结果写回原变量地址 */
    *value = out;
}
