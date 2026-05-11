/**
 * @file filter.c
 * @brief 基础软滤波
 * @details
 * 提供高频控制链路中使用的一阶低通滤波与固定窗口去极值平均滤波。
 * 所有运算都要求低延迟且不得阻塞。
 */
#include "zf_common_headfile.h"

/**
 * @brief 对浮点数组做升序排序
 * @param values 待排序数组
 * @param count 有效样本数
 * @details
 * 样本窗口固定很小（最大 5），这里使用直接插入排序，代码简单且中断内开销可控。
 */
static void filter_sort_float_asc(float *values, uint8 count)
{
    uint8 i;

    for (i = 1; i < count; i++)
    {
        float key;
        int8 j;

        key = values[i];
        j = (int8)i - 1;
        while (j >= 0 && values[(uint8)j] > key)
        {
            values[(uint8)j + 1] = values[(uint8)j];
            j--;
        }
        values[(uint8)j + 1] = key;
    }
}

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

/**
 * @brief 复位浮点去极值平均滤波状态
 * @param state 滤波状态
 */
void TrimmedMeanFilterFloatReset(TrimmedMeanFilterFloatState *state)
{
    uint8 i;

    if (0 == state)
    {
        return;
    }

    for (i = 0; i < 5; i++)
    {
        state->sample_hist[i] = 0.0f;
    }
    state->write_index = 0;
    state->valid_count = 0;
}

/**
 * @brief 浮点去极值平均滤波更新
 * @param state 滤波状态
 * @param sample 当前输入样本
 * @return float 去极值平均后的输出
 * @details
 * 采用固定 5 点窗口。样本数不足 3 时直接平均；达到 3 点后，每次去掉 1 个最大值和
 * 1 个最小值，仅对中间样本求平均，降低单点冲击对姿态量的影响。
 */
float TrimmedMeanFilterFloatUpdate(TrimmedMeanFilterFloatState *state, float sample)
{
    float work[5];
    float sum;
    uint8 copy_count;
    uint8 start_index;
    uint8 end_index;
    uint8 i;
    uint8 count;

    if (0 == state)
    {
        return sample;
    }

    state->sample_hist[state->write_index] = sample;
    state->write_index++;
    if (state->write_index >= 5)
    {
        state->write_index = 0;
    }

    if (state->valid_count < 5)
    {
        state->valid_count++;
    }

    count = state->valid_count;
    for (i = 0; i < count; i++)
    {
        work[i] = state->sample_hist[i];
    }

    filter_sort_float_asc(work, count);

    start_index = 0;
    end_index = count;
    if (count >= 3)
    {
        start_index = 1;
        end_index = count - 1;
    }

    sum = 0.0f;
    copy_count = 0;
    for (i = start_index; i < end_index; i++)
    {
        sum += work[i];
        copy_count++;
    }

    if (0 == copy_count)
    {
        return sample;
    }

    return sum / (float)copy_count;
}
