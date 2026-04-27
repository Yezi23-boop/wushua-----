/**
 * @file filter.c
 * @brief 编码器异常梳理与基础软滤波
 * @details
 * 提供对于带有噪声的反馈信号（特别是原始编码器）的低通滤波、中值滤波与强制符号纠错功能。
 * 由于是在高频中断 (run_time_1) 中直接调用，所有数字滤波运算都要求极低延迟且不得阻塞。
 */
#include "zf_common_headfile.h"

/**
 * @brief 返回编码器值的整数绝对值
 * @param value 输入编码器计数
 * @return 绝对值结果
 */
static int32 encoder_abs_int32(int32 value)
{
    if (value < 0)
    {
        return -value;
    }

    return value;
}

/**
 * @brief 根据零点死区提取编码器符号
 * @param raw_value 原始编码器计数
 * @return 1 表示正号，-1 表示负号，0 表示近零区
 */
static int8 encoder_sign_fix_get_sign(int32 raw_value)
{
    if (raw_value > ENC_ZERO_DEADBAND)
    {
        return 1;
    }

    if (raw_value < -ENC_ZERO_DEADBAND)
    {
        return -1;
    }

    return 0;
}

/**
 * @brief 将最新原始编码器计数压入 3 点历史窗口
 * @param state 编码器符号纠错状态
 * @param raw_now 当前原始编码器计数
 */
static void encoder_sign_fix_push_raw(EncoderSignFixState *state, int32 raw_now)
{
    if (state->hist_valid_count < 3)
    {
        state->raw_hist[state->hist_valid_count] = raw_now;
        state->hist_valid_count++;
    }
    else
    {
        state->raw_hist[0] = state->raw_hist[1];
        state->raw_hist[1] = state->raw_hist[2];
        state->raw_hist[2] = raw_now;
    }
}

/**
 * @brief 计算 3 个整数中的中值
 * @param a 样本 1
 * @param b 样本 2
 * @param c 样本 3
 * @return 3 个样本中的中间值
 */
static int32 encoder_median3_int32(int32 a, int32 b, int32 c)
{
    if (a > b)
    {
        int32 temp;

        temp = a;
        a = b;
        b = temp;
    }

    if (b > c)
    {
        int32 temp;

        temp = b;
        b = c;
        c = temp;
    }

    if (a > b)
    {
        b = a;
    }

    return b;
}

/**
 * @brief 将纠错后的样本压入 3 点中值窗口
 * @param state 编码器组合滤波状态
 * @param raw_now 当前纠错后的原始计数
 * @return 本次窗口对应的中值输出
 */
static int32 encoder_median3_push(EncoderMedian3EmaFilterState *state, int32 raw_now)
{
    int32 output;

    output = raw_now;

    if (state->median_valid_count < 2)
    {
        state->median_hist[state->median_valid_count] = raw_now;
        state->median_valid_count++;
    }
    else if (state->median_valid_count < 3)
    {
        state->median_hist[2] = raw_now;
        state->median_valid_count = 3;
        output = encoder_median3_int32(state->median_hist[0],
                                       state->median_hist[1],
                                       state->median_hist[2]);
    }
    else
    {
        state->median_hist[0] = state->median_hist[1];
        state->median_hist[1] = state->median_hist[2];
        state->median_hist[2] = raw_now;
        output = encoder_median3_int32(state->median_hist[0],
                                       state->median_hist[1],
                                       state->median_hist[2]);
    }

    return output;
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
 * @brief 基于最近 3 个原始编码器值修正单点符号误判
 * @details 仅修正“历史 3 点同号、当前点突然反号且幅值接近”的异常样本
 * @param raw_now 当前原始编码器计数
 * @param state 编码器符号纠错状态
 * @return 修正后的原始编码器计数
 */
int32 CorrectEncoderSignByHistory(int32 raw_now, EncoderSignFixState *state)
{
    int32 output;

    output = raw_now;

    if (0 != state && state->hist_valid_count >= 3)
    {
        int32 h0;
        int32 h1;
        int32 h2;
        int32 hist_abs_avg;
        int32 mag_delta;
        int8 hist_sign0;
        int8 hist_sign1;
        int8 hist_sign2;
        int8 current_sign;

        h0 = state->raw_hist[0];
        h1 = state->raw_hist[1];
        h2 = state->raw_hist[2];

        hist_sign0 = encoder_sign_fix_get_sign(h0);
        hist_sign1 = encoder_sign_fix_get_sign(h1);
        hist_sign2 = encoder_sign_fix_get_sign(h2);
        current_sign = encoder_sign_fix_get_sign(raw_now);

        if (0 != hist_sign0 &&
            hist_sign0 == hist_sign1 &&
            hist_sign1 == hist_sign2 &&
            0 != current_sign &&
            current_sign == -hist_sign0 &&
            encoder_abs_int32(raw_now) >= ENC_SIGN_FIX_MIN)
        {
            hist_abs_avg = (encoder_abs_int32(h0) +
                            encoder_abs_int32(h1) +
                            encoder_abs_int32(h2)) /
                           3;
            mag_delta = encoder_abs_int32(encoder_abs_int32(raw_now) - hist_abs_avg);

            if (mag_delta <= ENC_SIGN_MAG_TOL)
            {
                output = -raw_now;
            }
        }
    }

    if (0 != state)
    {
        encoder_sign_fix_push_raw(state, raw_now);
    }

    return output;
}

/**
 * @brief 编码器组合滤波：符号纠错 + 3 点中值 + EMA(1/2)
 * @details
 * 1. 先修正单点反号误判
 * 2. 再用 3 点中值压制孤立尖峰
 * 3. 最后用 EMA(1/2) 进一步平滑剩余抖动
 * @param raw_now 当前原始编码器计数
 * @param state 编码器组合滤波状态
 * @return 组合滤波后的原始编码器计数
 */
int32 FilterEncoderCountMedian3EmaHalf(int32 raw_now, EncoderMedian3EmaFilterState *state)
{
    int32 corrected;
    int32 median_out;

    if (0 == state)
    {
        return raw_now;
    }

    corrected = CorrectEncoderSignByHistory(raw_now, &state->sign_fix);
    median_out = encoder_median3_push(state, corrected);

    if (!state->ema_valid)
    {
        state->ema_out_raw = median_out;
        state->ema_valid = 1;
    }
    else
    {
        state->ema_out_raw = (state->ema_out_raw + median_out) / 2;
    }

    return state->ema_out_raw;
}
