#include "zf_common_headfile.h"
#define AD_VAL_MAX 3500 // 电感最大值上限（ADC 原始计数最大值，使用整数）
#define NUM 4           // 电感数量4
#define SORT_LENGTH 4   // 排序数组长度

/*********************************************
             电感排布
              四电感

    —     l       l     —

   ad1   ad2     ad3   ad4

 *********************************************/
static uint16 AD_value[NUM][SORT_LENGTH] = {{0}};                                // 存储电感原始值的二维数组（整数）
static uint16 adtemp = 0;                                                        // 排序临时变量（整数）
static uint32 ad_sum[NUM] = {0};                                                 // 电感值求和（防止溢出，使用32位）
static uint16 ad_ave[NUM] = {0};                                                 // 电感值中值（整数）
static uint16 AD_V[NUM] = {0}, AD_last[NUM] = {0};                               // 电感值处理后的结果（整数）
uint16 RAW[NUM] = {0};                                                           // 电感原始值（整数，供显示/标定）
uint16 ad1 = 0;                                                                  // 第1个电感值（归一化后，0-100）
uint16 ad2 = 0;                                                                  // 第2个电感值（归一化后，0-100）
uint16 ad3 = 0;                                                                  // 第6个电感值（归一化后，0-100）
uint16 ad4 = 0;                                                                  // 第7个电感值（归一化后，0-100）
static const uint16 MAX[NUM] = {AD_VAL_MAX, AD_VAL_MAX, AD_VAL_MAX, AD_VAL_MAX}; // 各电感最大值（常量）
uint16 MA[NUM] = {0};                                                            // 扫描记录的最大值（整数）
float Err = 0;                                                                   // 当前偏差值（整数，供 PID_Direction 使用）
static const int limit = 100;                                                    // 偏差限幅（整数）
void dispose(void)
{
    float denom = 0;
    denom = A_1 * (ad1 + ad4) + C_l * func_abs((int)ad2 - (int)ad3);
    Err = limit * (A_1 * (ad1 - ad4) + B_1 * (ad2 - ad3)) / denom;
}

/**
 * @brief 扫描赛道获取电感最大值
 * @details 获取各个电感的当前值，并更新记录的最大值
 * @note 用于电感标定和归一化计算的参考值
 */
void scan_track_max_value(void)
{
    int i = 0;
    // 读取 ADC 原始值（uint16），全程使用整数，避免不必要的类型转换
//    RAW[0] = adc_convert(ADC_CH1_P11); // 读取第1个电感AD值
//    RAW[1] = adc_convert(ADC_CH0_P10); // 读取第2个电感AD值
//    RAW[2] = adc_convert(ADC_CH8_P00); // 读取第3个电感AD值
//    RAW[3] = adc_convert(ADC_CH9_P01); // 读取第4个电感AD值
	
	  RAW[0] = adc_convert(ADC_CH9_P01); // 采集第1个电感（uint16）
      RAW[1] = adc_convert(ADC_CH8_P00); // 采集第2个电感（uint16）
      RAW[2] = adc_convert(ADC_CH0_P10); // 采集第3个电感（uint16）
      RAW[3] = adc_convert(ADC_CH1_P11); // 采集第4个电感（uint16）
    for (i = 0; i < NUM; i++)
    {
        // 如果当前值大于记录的最大值，则更新最大值
        if (RAW[i] > MA[i])
        {
            MA[i] = RAW[i];
        }
    }
}

/**
 * @brief 读取并处理电感数据
 * @details 多次采样并排序滤波，对电感值进行归一化处理并计算偏差
 * @note 核心电感处理函数，用于赛道识别和循迹控制
 */
void read_AD(void)
{
    int i, j, k;
    uint16 AD_ONE[NUM] = {0}; // 归一化后的电感值（0-100），使用整数定点
    int min_idx = 0;

    // 1. 多次采样电感值
    for (i = 0; i < SORT_LENGTH; i++)
    {
//	AD_value[0][i] = adc_convert(ADC_CH1_P11); // 采集第1个电感（uint16）
//	AD_value[1][i] = adc_convert(ADC_CH0_P10); // 采集第2个电感（uint16）
//	AD_value[2][i] = adc_convert(ADC_CH8_P00); // 采集第6个电感（uint16）
//	AD_value[3][i] = adc_convert(ADC_CH9_P01); // 采集第7个电感（uint16）
		
		AD_value[0][i] = adc_convert(ADC_CH9_P01); // 采集第1个电感（uint16）
        AD_value[1][i] = adc_convert(ADC_CH8_P00); // 采集第2个电感（uint16）
        AD_value[2][i] = adc_convert(ADC_CH0_P10); // 采集第3个电感（uint16）
        AD_value[3][i] = adc_convert(ADC_CH1_P11); // 采集第4个电感（uint16）
    }

    // 2. 对每个电感的采样值进行排序（选择排序算法）
    for (i = 0; i < NUM; i++)
    {
        for (j = 0; j < SORT_LENGTH - 1; j++)
        {
            min_idx = j;
            for (k = j + 1; k < SORT_LENGTH; k++)
            {
                if (AD_value[i][k] < AD_value[i][min_idx])
                {
                    min_idx = k;
                }
            }
            if (min_idx != j)
            {
                adtemp = AD_value[i][j];
                AD_value[i][j] = AD_value[i][min_idx];
                AD_value[i][min_idx] = adtemp;
            }
        }

        // 3. 中值计算（整数）- 数组长度为4，取中间两个值(下标1和2)的平均值
        ad_sum[i] = (uint32)AD_value[i][1] + (uint32)AD_value[i][2];
        ad_ave[i] = (uint16)((ad_sum[i] + 1u) / 2u); // +1做四舍五入
        AD_value[i][SORT_LENGTH - 1] = ad_ave[i];
    }

    memset(ad_sum, 0, sizeof(ad_sum));

    // 4. 计算每个电感的最终值并限幅
    for (i = 0; i < NUM; i++)
    {
        for (j = 0; j < SORT_LENGTH; j++)
        {
            ad_sum[i] += (uint32)AD_value[i][j];
        }
        AD_V[i] = (uint16)(ad_sum[i] / SORT_LENGTH); // 求平均值（整数）
        RAW[i] = AD_V[i];                            // 保存原始AD值（整数）
        if (AD_V[i] > MAX[i])                        // 限幅处理
        {
            AD_V[i] = MAX[i];
        }
    }

    // 5. 归一化处理（转换为0-100范围）
    for (i = 0; i < NUM; i++)
    {
        // 使用整数定点：百分比 = AD_V / MAX * 100
        AD_ONE[i] = (uint16)((100u * (uint32)AD_V[i]) / (uint32)MAX[i]);
    }

    // 6. 将处理后的值赋给各个电感变量
    ad1 = AD_ONE[0];
    ad2 = AD_ONE[1];
    ad3 = AD_ONE[2];
    ad4 = AD_ONE[3];
    dispose(); // 处理电感数据，计算偏差值
}
