#ifndef __FILTER_H__
#define __FILTER_H__

/*********************************************
 * 低通滤波器结构体
 *********************************************/
typedef struct
{
    float out_last; // 上一次滤波值
} LowPassFilter_t;
void low_pass_filter_mt(LowPassFilter_t *filter, float *value, float alpha);
#endif
