#include "zf_common_headfile.h"

float test_speed_value = 0;
float test_angle_value = 0;

void test_angle_func(void)
{
    Prepare_Data();
    Encoder_get(&PID.left_speed, &PID.right_speed);
    // pid_steer_update(&PID.steer, Err, -gyro_z * 0.01);                // 更新转向环
    pid_angle_update(&PID.angle, test_angle_value, gyro_z * 0.082);               // 更新角度环
    pid_speed_update(&PID.left_speed, -PID.angle.output, PID.left_speed.speed);   // 更新左轮速度环
    pid_speed_update(&PID.right_speed, +PID.angle.output, PID.right_speed.speed); // 更新右轮速度环
    motor_output((int)PID.left_speed.output, (int)PID.right_speed.output);
}

void test_speed_func(void)
{
    //    int32 ff_l;
    //    int32 ff_r;
//    int lpwm;
//    int rpwm;
    //	static int test_time = 0;
    //	test_time++;
    Encoder_get(&PID.left_speed, &PID.right_speed);
    //	if (test_time >= 4000)
    //	{
    //		test_speed_value = 60;
    //		test_time = 0;
    //	}
    //	else if (test_time >= 3000)
    //	{
    //		test_speed_value = -40;
    //	}
    //	else if (test_time >= 2000)
    //	{
    //		test_speed_value = 18;
    //	}
    //	else if (test_time >= 1000)
    //	{
    //		test_speed_value = 15;
    //	}
    //	else if (test_time >= 500)
    //	{
    //		test_speed_value = 20;
    //	}
    pid_speed_update(&PID.left_speed, test_speed_value, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, test_speed_value, PID.right_speed.speed);

    //    ff_l = motor_speed_to_duty(test_speed_value);
    //    ff_r = motor_speed_to_duty(test_speed_value);

    //    lpwm = ff_l + (int32)PID.left_speed.output;
    //    rpwm = ff_r + (int32)PID.right_speed.output;
    // if (lpwm > PWM_DUTY_MAX)
    //     lpwm = PWM_DUTY_MAX;
    // if (lpwm < -PWM_DUTY_MAX)
    //     lpwm = -PWM_DUTY_MAX;
    // if (rpwm > PWM_DUTY_MAX)
    //     rpwm = PWM_DUTY_MAX;
    // if (rpwm < -PWM_DUTY_MAX)
    //     rpwm = -PWM_DUTY_MAX;
    //    motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
    motor_output((int32)PID.left_speed.output, (int32)PID.right_speed.output);
//		  motor_output(1000, 1000);
}
