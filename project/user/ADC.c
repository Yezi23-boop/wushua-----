#include "zf_common_headfile.h"
#include "ADC.h"

/* 内部常量定义 */
#define ADC_RAW_MAX 3500 /* ADC 原始采样的理论最大有效值 */
#define ADC_NORM_MAX 100 /* 归一化后的量程上限 */
#define SORT_LENGTH 4    /* 滑动排序/均值滤波的样本长度 */

/* 内部中间变量 */
static uint16 AD_value[NUM][SORT_LENGTH] = {{0}}; /* 滤波缓冲区 */
static uint16 adtemp = 0;                         /* 排序交换临时变量 */
static uint32 ad_sum[NUM] = {0};                  /* 累加和 */
static uint16 ad_ave[NUM] = {0};                  /* 平均值 */
static uint16 AD_V[NUM] = {0};                    /* 当前周期的处理后值 */
static uint8 adc_measure_enable = 1;              /* 默认开启最大值动态记录 */

/* 默认标定参数（若无 EEPROM 加载则使用此值） */
static const uint16 MIN_Err[NUM] = {0, 0, 0, 0};
static const uint16 MAX_Err[NUM] = {ADC_RAW_MAX, ADC_RAW_MAX, ADC_RAW_MAX, ADC_RAW_MAX};
static const int limit = 100;

/* 全局导出变量 */
volatile uint16 RAW[NUM] = {0};
volatile uint16 MA[NUM] = {0};
volatile uint16 MI[NUM] = {ADC_RAW_MAX, ADC_RAW_MAX, ADC_RAW_MAX, ADC_RAW_MAX};
uint16 ad1 = 0;
uint16 ad2 = 0;
uint16 ad3 = 0;
uint16 ad4 = 0;
volatile float Err = 0.0f;

/* 内部私有函数声明 */
static void adc_read_channels(uint16 *raw_buffer);
static uint16 adc_normalize_value(uint16 raw_value, uint16 min_value, uint16 max_value);
static void dispose(uint16 ad1, uint16 ad2, uint16 ad3, uint16 ad4);

/**
 * @brief 处理电感偏差计算
 * @details 采用四路电感的差比和算法，并支持参数 A_1, B_1, C_l 的加权修正
 */
static void dispose(uint16 ad11, uint16 ad22, uint16 ad33, uint16 ad44)
{
    float denom;
    int16 diff14;
    int16 diff23;
    uint16 sum14;

    diff14 = (int16)ad11 - (int16)ad44;
    diff23 = (int16)ad22 - (int16)ad33;
    sum14 = ad11 + ad44;

    denom = app.angle.A_1 * (float)sum14 +
            app.angle.C_l * (float)func_abs(diff23);

    if (denom < 1.0f)
    {
        Err = 0.0f;
        return;
    }

    Err = (float)limit *
          (app.angle.A_1 * (float)diff14 +
           app.angle.B_1 * (float)diff23) /
          denom;
}

/**
 * @brief 动态扫描电感的最大/最小值（用于自动标定）
 */
void scan_track_max_value(void)
{
    int i;

    if (!adc_measure_enable)
        return;

    for (i = 0; i < NUM; i++)
    {
        if (RAW[i] == 0u)
            continue;

        /* 更新历史最大值 */
        if (RAW[i] > MA[i])
            MA[i] = RAW[i];

        /* 更新历史最小值 */
        if (RAW[i] < MI[i])
            MI[i] = RAW[i];
    }
}

/**
 * @brief 执行电感数据读取与处理全流程
 * @details 包含：多通道采样 -> 冒泡排序 -> 去极值均值滤波 -> 归一化 -> 偏差计算
 */
void read_AD(void)
{
    int i, j, k, min_idx;
    uint16 raw_buffer[NUM];
    uint16 AD_ONE[NUM];

    /* 1. 多次采样填充缓冲区 */
    for (i = 0; i < SORT_LENGTH; i++)
    {
        adc_read_channels(raw_buffer);
        for (j = 0; j < NUM; j++)
            AD_value[j][i] = raw_buffer[j];
    }

    /* 2. 对每个通道进行排序和基础滤波 */
    for (i = 0; i < NUM; i++)
    {
        for (j = 0; j < SORT_LENGTH - 1; j++)
        {
            min_idx = j;
            for (k = j + 1; k < SORT_LENGTH; k++)
            {
                if (AD_value[i][k] < AD_value[i][min_idx])
                    min_idx = k;
            }
            if (min_idx != j)
            {
                adtemp = AD_value[i][j];
                AD_value[i][j] = AD_value[i][min_idx];
                AD_value[i][min_idx] = adtemp;
            }
        }

        /* 取排序后的中间项计算均值，更新样本 */
        ad_sum[i] = (uint32)AD_value[i][1] + (uint32)AD_value[i][2];
        ad_ave[i] = (uint16)((ad_sum[i] + 1u) / 2u);
        AD_value[i][SORT_LENGTH - 1] = ad_ave[i];
    }

    /* 3. 计算最终均值并进行初步限幅 */
    memset(ad_sum, 0, sizeof(ad_sum));
    for (i = 0; i < NUM; i++)
    {
        for (j = 0; j < SORT_LENGTH; j++)
            ad_sum[i] += (uint32)AD_value[i][j];

        AD_V[i] = (uint16)(ad_sum[i] / SORT_LENGTH);
        RAW[i] = AD_V[i]; /* 保存原始值用于调试和标定 */

        if (AD_V[i] > MAX_Err[i])
            AD_V[i] = MAX_Err[i];
    }

    /* 4. 归一化映射 (映射到 0~100) */
    for (i = 0; i < NUM; i++)
    {
        AD_ONE[i] = adc_normalize_value(AD_V[i], MIN_Err[i], MAX_Err[i]);
    }

    /* 5. 分配给全局变量 */
    ad1 = AD_ONE[0];
    ad2 = AD_ONE[1];
    ad3 = AD_ONE[2];
    ad4 = AD_ONE[3];

    /* 6. 执行偏差解算 */
    dispose(ad1, ad2, ad3, ad4);
}

/**
 * @brief 使能或禁止动态最大值记录
 */
void adc_measure_set_enable(uint8 enable)
{
    adc_measure_enable = enable;
}

/**
 * @brief 重置标定记录
 */
void adc_measure_reset(void)
{
    int i;
    for (i = 0; i < NUM; i++)
    {
        MA[i] = 0u;
        MI[i] = ADC_RAW_MAX;
    }
}

/**
 * @brief 读取硬件 ADC 通道
 */
static void adc_read_channels(uint16 *raw_buffer)
{
    raw_buffer[0] = adc_convert(ADC_CH9_P01); /* 左横电感 */
    raw_buffer[1] = adc_convert(ADC_CH8_P00); /* 左竖电感 */
    raw_buffer[2] = adc_convert(ADC_CH0_P10); /* 右横电感 */
    raw_buffer[3] = adc_convert(ADC_CH1_P11); /* 右竖电感 */
}

/**
 * @brief 线性归一化函数
 */
static uint16 adc_normalize_value(uint16 raw, uint16 min, uint16 max)
{
    uint16 span;
    uint32 scaled;

    if (max <= min)
    {
        min = 0;
        max = ADC_RAW_MAX;
    }

    span = max - min;
    if (raw <= min)
        return 0;
    if (raw >= max)
        return ADC_NORM_MAX;

    scaled = (uint32)(raw - min) * ADC_NORM_MAX;
    return (uint16)((scaled + (uint32)span / 2u) / (uint32)span);
}
