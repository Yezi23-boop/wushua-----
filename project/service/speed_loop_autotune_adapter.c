/**
 * @file speed_loop_autotune_adapter.c
 * @brief 自动调参系统串口与数据采集适配桥
 * @details
 * 提供 Python AI 调参套件所需的数据接口与下放序列化采集。
 * 这个文件不涉及实际高速控制逻辑的改变，而是统一收拢上报或指令解析层调用。
 */
#include "zf_common_headfile.h"
#include "motor.h"
#include "pid.h"
#include "../user/FUYA.h"
#include "../user/a_run.h"
#include "../speed_loop_autotune/firmware/speed_loop_autotune.h"
#include "speed_loop_autotune_adapter.h"

static speed_loop_autotune_binding_t speed_loop_autotune_project_binding;
static uint8 speed_loop_autotune_project_binding_ready = 0;

static void speed_loop_autotune_project_read_speed(float *left_speed, float *right_speed)
{
    Encoder_get(&PID.left_speed, &PID.right_speed);

    if (left_speed != 0)
    {
        *left_speed = PID.left_speed.speed;
    }
    if (right_speed != 0)
    {
        *right_speed = PID.right_speed.speed;
    }
}

static void speed_loop_autotune_project_write_motor_pwm(int32 left_pwm, int32 right_pwm)
{
    motor_output(left_pwm, right_pwm);
}

static void speed_loop_autotune_project_write_fuya_pwm(int16 pwm_value)
{
    fuya_set_pwm((int)pwm_value);
}

static void speed_loop_autotune_project_set_drive_state(uint8 enable, int flat_state)
{
    (void)flat_state;

    if (enable)
    {
        stop = 0;
    }
    else
    {
        stop = 1;
    }
}

static void speed_loop_autotune_project_force_stop(void)
{
    pwm_set_duty(PWMB_CH2_P13, 0);
    pwm_set_duty(PWMB_CH3_P52, 0);
}

static uint8 speed_loop_autotune_project_read_stop_flag(void)
{
    return (uint8)(stop != 0);
}

static const speed_loop_autotune_port_t speed_loop_autotune_project_port = {
    speed_loop_autotune_project_read_speed,
    speed_loop_autotune_project_write_motor_pwm,
    speed_loop_autotune_project_write_fuya_pwm,
    speed_loop_autotune_project_set_drive_state,
    speed_loop_autotune_project_force_stop,
    speed_loop_autotune_project_read_stop_flag,
};

static void speed_loop_autotune_project_bind_once(void)
{
    if (speed_loop_autotune_project_binding_ready)
    {
        return;
    }

    speed_loop_autotune_binding_init(&speed_loop_autotune_project_binding);
    speed_loop_autotune_binding_bind(
        &speed_loop_autotune_project_binding,
        SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KP,
        &PID.left_speed.Kp,
        "LEFT_KP");
    speed_loop_autotune_binding_bind(
        &speed_loop_autotune_project_binding,
        SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KI,
        &PID.left_speed.Ki,
        "LEFT_KI");
    speed_loop_autotune_binding_bind(
        &speed_loop_autotune_project_binding,
        SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KD,
        &PID.left_speed.Kd,
        "LEFT_KD");
    speed_loop_autotune_binding_bind(
        &speed_loop_autotune_project_binding,
        SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KP,
        &PID.right_speed.Kp,
        "RIGHT_KP");
    speed_loop_autotune_binding_bind(
        &speed_loop_autotune_project_binding,
        SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KI,
        &PID.right_speed.Ki,
        "RIGHT_KI");
    speed_loop_autotune_binding_bind(
        &speed_loop_autotune_project_binding,
        SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KD,
        &PID.right_speed.Kd,
        "RIGHT_KD");
    speed_loop_autotune_project_binding_ready = 1;
}

void speed_loop_autotune_project_init(void)
{
    speed_loop_autotune_project_bind_once();
    speed_loop_autotune_component_init(
        &speed_loop_autotune_project_binding,
        &speed_loop_autotune_project_port);
}
