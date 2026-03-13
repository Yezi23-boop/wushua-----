#include "zf_common_headfile.h"

/*********************************************
 * 一阶低通滤波器
 *********************************************/
void low_pass_filter_mt(LowPassFilter_t *filter, float *value, float alpha)
{
	float out;

	// 一阶低通滤波计算：新值 = alpha * 当前值 + (1-alpha) * 上次值
	out = alpha * (*value) + (1 - alpha) * filter->out_last;
	filter->out_last = out;

	// 将滤波结果写回到value指向的变量
	*value = out;
}
