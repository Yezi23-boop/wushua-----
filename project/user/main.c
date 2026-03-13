#include "zf_common_headfile.h"
#include <stdlib.h>

// 打印与调试函数声明
void printf_adc();
void printf_imu();
void printf_date();
void printf_speed_test();
void vofa();
void main()
{

	clock_init(SYSTEM_CLOCK_40M); // 时钟初始化
	debug_init();				  // 调试接口初始化
	P32 = 1;					  // 上电安全记录（示例）
	// 初始化（wireless_uart_init 已在 int_user 中完成）
	int_user();

	while (1)
	{
//    vofa();
	Keystroke_Menu();
//	printf_speed_test();
	}
}

void vofa(void)
{
    static char vofa_cmd[64]; // VOFA 命令缓存
	vofa_parse_from_fifo();
	 // 检查是否解析到完整命令
	if (vofa_get_command(vofa_cmd, 64))
	{
		handle_vofa_command(vofa_cmd);
	}
//	printf("%f,%f,%f,%f\n", test_angle_value, gyro_z, gyro_z * 0.082, PID.angle.output);
	printf("%f,%f,%f,%f\n",test_speed_value, PID.left_speed.speed, PID.right_speed.speed, 0.0);
}
// 原有的数据显示函数（已部分调整）
void printf_date()
{
	// 优化：Err 与电感归一化值使用整数显示，避免浮点格式化开销
	ips114_show_int32(1 * 24, 18 * 0, Err, 3);
	ips114_show_int32(1 * 24, 18 * 1, ad1, 3);
	ips114_show_int32(1 * 24, 18 * 2, ad2, 3);
	ips114_show_int32(1 * 24, 18 * 3, ad3, 3);
	ips114_show_int32(1 * 24, 18 * 4, ad4, 3);

	ips114_show_float(3 * 24, 18 * 0, PID.steer.output, 3, 1);
	ips114_show_float(3 * 24, 18 * 1, speed_run + PID.steer.output, 3, 1);
	ips114_show_float(3 * 24, 18 * 2, speed_run - PID.steer.output, 3, 1);

	ips114_show_int32(6 * 24, 18 * 1, dianya, 5);
}

void printf_adc()
{
	// 电感归一化值（0-100）整数显示
	ips114_show_int32(1 * 24, 18 * 0, ad1, 3);
	ips114_show_int32(1 * 24, 18 * 1, ad2, 3);
	ips114_show_int32(1 * 24, 18 * 2, ad3, 3);
	ips114_show_int32(1 * 24, 18 * 3, ad4, 3);

	ips114_show_int32(1 * 24, 18 * 6, Err, 4);

	// 原始 ADC 计数显示（整数）
	ips114_show_int32(3 * 24, 18 * 0, RAW[0], 4);
	ips114_show_int32(3 * 24, 18 * 1, RAW[1], 4);
	ips114_show_int32(3 * 24, 18 * 2, RAW[2], 4);
	ips114_show_int32(3 * 24, 18 * 3, RAW[3], 4);

	// 标定最大值显示（整数）
	ips114_show_int32(7 * 24, 18 * 0, MA[0], 4);
	ips114_show_int32(7 * 24, 18 * 1, MA[1], 4);
	ips114_show_int32(7 * 24, 18 * 2, MA[2], 4);
	ips114_show_int32(7 * 24, 18 * 3, MA[3], 4);
}

void printf_imu()
{
	ips114_show_float(4 * 24, 18 * 0, Gyr_filt.X, 4, 2);
	ips114_show_float(4 * 24, 18 * 1, Gyr_filt.Y, 4, 2);
	ips114_show_float(4 * 24, 18 * 2, Gyr_filt.Z, 4, 2);
	ips114_show_float(4 * 24, 18 * 4, Acc_filt.X, 4, 2);
	ips114_show_float(4 * 24, 18 * 5, Acc_filt.Y, 4, 2);
	ips114_show_float(4 * 24, 18 * 6, Acc_filt.Z, 4, 2);
	ips114_show_float(7 * 24, 18 * 0, vx, 4, 2);
	ips114_show_float(7 * 24, 18 * 1, vy, 4, 2);
	ips114_show_float(7 * 24, 18 * 2, vz, 4, 2);
	ips114_show_int32(7 * 24, 18 * 4, fuya_date, 4);
	ips114_show_int32(7 * 24, 18 * 5, phase, 4);
}

void printf_speed_test()
{
	ips114_show_float(0, 0, test_angle_value, 6, 1);
	ips114_show_float(0, 15, PID.angle.output, 6, 1);
	ips114_show_float(0, 35, speed_l, 6, 1);
	ips114_show_float(0, 55, speed_r, 6, 1);
	ips114_show_float(0, 75, gyro_z, 6, 2);
	ips114_show_float(0, 95, gyro_z * 0.1, 6, 2);
	printf("%f,%f,%f,%f\n", test_angle_value, gyro_z, gyro_z * 0.082, PID.angle.output);
}

void printf_butten_test()
{
	ips114_show_int32(4 * 24, 18 * 0, P33, 1);
	ips114_show_int32(4 * 24, 18 * 1, P34, 1);
	ips114_show_int32(4 * 24, 18 * 2, P35, 1);
	ips114_show_int32(4 * 24, 18 * 4, P36, 1);
	ips114_show_int32(4 * 24, 18 * 5, P37, 1);
	ips114_show_int32(4 * 24, 18 * 6, flat_statr, 2);
}
