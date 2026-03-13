#include "motor.h"

// 电机与外设初始化函数
void motor_Init()
{
	// 左电机 PWM 初始化：使用 PWMB_CH2 (P13引脚)，频率 17kHz，初始占空比 0
	pwm_init(PWMB_CH2_P13, 17000, 0);
	// 左电机方向引脚初始化：P14 设置为推挽输出，默认高电平
	gpio_init(IO_P14, GPO, 1, GPO_PUSH_PULL);

	// 右电机 PWM 初始化：使用 PWMB_CH3 (P52引脚)，频率 17kHz，初始占空比 0
	pwm_init(PWMB_CH3_P52, 17000, 0);
	// 右电机方向引脚初始化：P53 设置为推挽输出，默认高电平
	gpio_init(IO_P53, GPO, 1, GPO_PUSH_PULL);
}

// 全局控制标志位
uint8 stop = 0;	  // 停车标志：1-强制停车，0-正常运行
float dianya = 0; // 电池电压（单位：V）

// 电机输出控制函数
// 参数：lpwm - 左电机占空比（正数正转，负数反转）
//       rpwm - 右电机占空比（正数正转，负数反转）
void motor_output(int32 lpwm, int32 rpwm)
{
//	if(func_abs(lpwm)<500)
//	{
//		if(lpwm>100)
//		{
//		lpwm=500;
//		}
//		if(lpwm<0)
//		{
//		lpwm=-500;
//		}
//	}
//	if(func_abs(rpwm)<500)
//	{
//		if(rpwm>0)
//		{
//		rpwm=500;
//		}
//		if(rpwm<0)
//		{
//		rpwm=-500;
//		}
//	}
	if (stop == 0) // 正常运行模式
	{
		// --- 左电机控制 ---
		if (rpwm > 0)
		{
			P14 = 0;						  // 正转方向
			pwm_set_duty(PWMB_CH2_P13, rpwm); // 设置占空比
		}
		else if (rpwm < 0)
		{
			P14 = 1;						   // 反转方向
			pwm_set_duty(PWMB_CH2_P13, -rpwm); // 占空比取绝对值
		}

		// --- 右电机控制 ---
		if (lpwm > 0)
		{
			P53 = 1;						  // 正转方向（硬件接线可能与左侧相反）
			pwm_set_duty(PWMB_CH3_P52, lpwm); // 设置占空比
		}
		else if (lpwm < 0)
		{
			P53 = 0;						   // 反转方向
			pwm_set_duty(PWMB_CH3_P52, -lpwm); // 占空比取绝对值
		}
	}
	else // 停车模式（stop == 1）
	{
		// 强制输出极小占空比（或特定值），通常应设为 0 停止电机
		// 此处设为 100 可能用于维持某种状态或微弱制动，具体视驱动器特性而定
		pwm_set_duty(PWMB_CH2_P13, 100);
		pwm_set_duty(PWMB_CH3_P52, 100);
	}
}

// 丢线保护函数
// 检测四路电感值，若全部极低则判定为丢线
void lost_lines()
{
	static int8 count = 0; // 丢线计数器
	// 四路电感值均小于阈值 3，认为未检测到赛道
	if (ad1 < 3 && ad2 < 3 && ad3 < 3 && ad4 < 3)
	{
		count++;
	}
	// 连续 5 次检测到丢线，触发停车保护
	if (count > 5)
	{
		count = 0;
		stop = 1; // 置位停车标志
	}
}

// 电压监测保护函数
// 采样电池电压，低压时触发保护
void dianya_jiance(void)
{
	static int32 dianya_count = 0; // 低压计数器
	// 读取 ADC 值并转换为电压（系数 0.0092 需根据分压电阻标定）
	uint16 adc_raw = adc_convert(ADC_CH13_P05);
	dianya = adc_raw * 0.0092;

	// 电压低于 7.4V 判定为低压
	if (dianya < 11.3)
	{
		dianya_count++;
	}
	// 连续 1000 次低压（防止瞬时跌落误判），触发停车
	if (dianya_count > 3000)
	{
		stop = 1; // 保护电池，防止过放
	}
}

// --- 查表前馈数据 ---
// 速度点集（实测编码器速度）
static const float ff_speed_points[] = {
	0.0f, 5.4f, 13.0f, 18.4f, 22.2f, 27.6f, 34.2f, 38.0f,
	45.8f, 49.6f, 57.2f, 64.8f, 67.8f, 76.0f, 81.8f, 88.4f, 93.8f};
// 占空比点集（对应速度所需的 PWM 占空比）
static const int16 ff_duty_points[] = {
	0, 500, 1000, 1500, 2000, 2500, 3000, 3500,
	4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000};

// 前馈查表函数：根据目标速度计算基础占空比
// 参数：speed - 目标速度
//       side  - 电机侧（0:左, 1:右），目前左右共用一张表
// 返回：对应的 PWM 占空比
int32 motor_speed_to_duty(float speed)
{
	float s;	// 速度绝对值
	int i;		// 循环索引
	int32 duty; // 计算结果

	// 取速度绝对值，因为前馈表基于正向测试数据
	if (speed > 0.0f)
	{
		s = speed;
	}
	else if (speed < 0.0f)
	{
		s = -speed;
	}
	else
	{
		return 0; // 速度为 0 直接返回 0
	}

	// 遍历查表，寻找速度所在的区间
	// 表中有 17 个点，区间索引 0 到 15
	for (i = 0; i < 16; i++)
	{
		// 找到区间 [i, i+1]，使得 ff_speed_points[i] < s <= ff_speed_points[i+1]
		if (s <= ff_speed_points[i + 1])
		{
			// 取区间端点值
			float x0 = ff_speed_points[i];
			float x1 = ff_speed_points[i + 1];
			int32 y0 = ff_duty_points[i];
			int32 y1 = ff_duty_points[i + 1];

			// 线性插值公式：y = y0 + (x - x0) * (y1 - y0) / (x1 - x0)
			float t = (s - x0) / (x1 - x0); // 计算归一化比例 t
			duty = (int32)(y0 + t * (y1 - y0));

			// 根据原始速度符号恢复占空比方向
			if (speed < 0.0f)
			{
				duty = -duty;
			}

			// 安全限幅
			if (duty > PWM_DUTY_MAX)
			{
				duty = PWM_DUTY_MAX;
			}
			if (duty < -PWM_DUTY_MAX)
			{
				duty = -PWM_DUTY_MAX;
			}
			return duty; // 返回插值结果
		}
	}

	// 若速度超过表中最大值，直接使用最大点对应的占空比
	duty = ff_duty_points[16];
	if (speed < 0.0f)
	{
		duty = -duty;
	}
	return duty;
}
