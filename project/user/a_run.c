#include "zf_common_headfile.h"

int flat_statr = 0;
static int flat_statr_date = 0;
static int time_1;

static int speed_active = 0;
static int count_fly_1 = 0;
static int count_fly_2 = 0;

int flat_fly = 0;
void fly_slow_update(int *speed);
void run_time_1(void)
{
    if (!P32)
    {
        IAP_CONTR = 0x60; // 判断快速烧录
    }
    read_AD(); // 读取并处理电感数据
    time_1++;
    Prepare_Data();
    Encoder_get(&PID.left_speed, &PID.right_speed);
    if (time_1 == 2)
    {
        pid_steer_update(&PID.steer, Err); // 更新转向环
        time_1 = 0;
    }
    fly_slow_update(&speed_active);
    pid_angle_update(&PID.angle, PID.steer.output, gyro_z * 0.082);                             // 更新角度环
    pid_speed_update(&PID.left_speed, speed_active - PID.angle.output, PID.left_speed.speed);   // 更新左轮速度环
    pid_speed_update(&PID.right_speed, speed_active + PID.angle.output, PID.right_speed.speed); // 更新右轮速度环
    if (flat_statr >= 2)
    {
        motor_output((int)PID.left_speed.output, (int)PID.right_speed.output);
    }
//	  motor_output(4000, 4000);
}

void run_time_2(void)
{
    scan_track_max_value(); // 获取电感最大值
    lost_lines();
    dianya_jiance();
    IMUupdate(&Gyr_filt, &Acc_filt, &Att_Angle);
    flat_statr_date++;
    if (P35 == 0 && flat_statr_date > 50)
    {
        flat_statr++;
        flat_statr_date = 0;
    }
    if (flat_statr >= 1 && start_flag == 1)
    {
        fuya_update_simple();
    }
}

void run_time_3(void)
{
    float delta = 0.0f;
    float left_target = 0.0f;
    float right_target = 0.0f;
    float dec_gain = 0.0f;
    float acc_gain = 0.0f;
    float abs_delta = 0.0f;
    if (!P32)
    {
        IAP_CONTR = 0x60; // 判断快速烧录
    }
    read_AD(); // 读取并处理电感数据
    Prepare_Data();
    Encoder_get(&PID.left_speed, &PID.right_speed);
    pid_steer_update(&PID.steer, Err);                                      // 更新转向环，增加陀螺仪抑制甩尾
    pid_angle_update(&PID.angle, PID.steer.output, imu660ra_gyro_z * 0.01); // 更新角度环

    // 方法1：使用原来的 PID 差速
    // Pid_Differential(speed_run, &left_target, &right_target, 500);

    // 方法2：使用纯追踪 (Pure Pursuit) 算法
    // 假设 Err 最大值约为 100.0 (需根据实际电感数据范围调整归一化分母)
    // 如果 Err 是原始 ADC 差值(如几千)，这里必须除以最大可能值
    /*
    {
        float norm_err = Err / 100.0f;
        Pure_Pursuit_Control(speed_run, norm_err, &left_target, &right_target);
    }
    */

    // 方法3：使用纯追踪 + 陀螺仪混合控制 (推荐终极方案)
    {
        // 1. 归一化电感误差 (-1.0 ~ 1.0)
        // 请务必确认你的 Err 最大值是多少！如果 Err 是原始 ADC 差值 (如 ±3000)，这里要除以 3000.0f
        float norm_err = Err / 100.0f;

        // 2. 获取陀螺仪 Z 轴角速度
        // 注意：Pure_Pursuit_Gyro_Control 内部假设 gyro_z 单位是 度/秒
        // 如果 imu660ra_gyro_z 是原始值，需确认其单位
        float gyro_z = imu660ra_gyro_z * 0.01f; // 假设这里换算后是 度/秒

        Pure_Pursuit_Gyro_Control(speed_run, norm_err, gyro_z, &left_target, &right_target);
    }

    pid_speed_update(&PID.left_speed, left_target, PID.left_speed.speed);
    pid_speed_update(&PID.right_speed, right_target, PID.right_speed.speed);
    if (flat_statr >= 2)
    {
        motor_output((int)PID.left_speed.output, (int)PID.right_speed.output);
        //	 motor_output(1000, 1000);
    }
    //    test_speed();
}

void run_test_speed()
{    if (!P32)
    {
        IAP_CONTR = 0x60; // 判断快速烧录
    }
    test_speed_func();
}
void run_test_angle()
{    if (!P32)
    {
        IAP_CONTR = 0x60; // 判断快速烧录
    }
    test_angle_func();
    fuya_update_simple();
}
void fly_slow_update(int *speed)
{
    if (ad1 < 40 && ad2 < 15 && ad3 < 15 && ad4 < 40 && flat_fly == 0)
    {
        count_fly_1++;
        if (count_fly_1 >= count_fly_time_1)
        {
            count_fly_1 = 0;
            flat_fly = 1;
        }
    }
    if (flat_fly == 1)
    {
        *speed = count_fly_speed;
        PID.steer.output = count_fly_angle;
        count_fly_2++;
        if (count_fly_2 >= count_fly_time_2)
        {
            count_fly_2 = 0;
            flat_fly = 0;
        }
    }
    else
    {
        *speed = speed_run;
    }
}