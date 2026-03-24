#include "zf_common_headfile.h"
#include "speed_loop_autotune_private.h"
#include "autotune_runtime.h"

volatile uint8 test_trial_active = 0;
volatile uint8 test_trial_armed = 0;
volatile uint16 test_trial_elapsed_ms = 0;
volatile uint16 test_trial_limit_ms = 250;
volatile uint16 test_trial_cooldown_elapsed_ms = 450;
volatile uint16 test_trial_cooldown_ms = 450;
volatile int16 test_fuya_pwm = 2000;
volatile int32 test_left_pwm_output = 0;
volatile int32 test_right_pwm_output = 0;
volatile int16 test_left_pwm_cmd = 0;
volatile int16 test_right_pwm_cmd = 0;
volatile uint8 test_tick_5ms_count = 0;
volatile uint8 test_control_mode = AUTOTUNE_TEST_MODE_SPEED;

static int16 speed_loop_autotune_clamp_pwm(float value)
{
    if (value < 0.0f)
    {
        value = 0.0f;
    }
    if (value > 4000.0f)
    {
        value = 4000.0f;
    }
    return (int16)value;
}

void speed_loop_autotune_force_stop_output(void)
{
    speed_loop_autotune_component_force_stop_output();
}

void speed_loop_autotune_reset_runtime(uint8 keep_armed, uint8 start_cooldown)
{
    test_trial_active = 0;
    test_trial_elapsed_ms = 0;

    if (!keep_armed)
    {
        test_trial_armed = 0;
    }

    speed_loop_autotune_component_reset_control();
    speed_loop_autotune_component_set_drive_enabled(0, 0);
    test_left_pwm_cmd = 0;
    test_right_pwm_cmd = 0;
    test_left_pwm_output = 0;
    test_right_pwm_output = 0;
    test_control_mode = AUTOTUNE_TEST_MODE_SPEED;
    speed_loop_autotune_force_stop_output();
    speed_loop_autotune_component_write_fuya(0);

    if (start_cooldown)
    {
        test_trial_cooldown_elapsed_ms = 0;
    }
    else
    {
        test_trial_cooldown_elapsed_ms = test_trial_cooldown_ms;
    }
}

void speed_loop_autotune_start_drive(void)
{
    speed_loop_autotune_component_set_drive_enabled(1, 3);
}

void speed_loop_autotune_set_test_mode(uint8 mode)
{
    if (mode != AUTOTUNE_TEST_MODE_PWM_IDENTIFY)
    {
        mode = AUTOTUNE_TEST_MODE_SPEED;
    }

    if (test_control_mode != mode)
    {
        speed_loop_autotune_component_reset_control();
        test_left_pwm_cmd = 0;
        test_right_pwm_cmd = 0;
        test_left_pwm_output = 0;
        test_right_pwm_output = 0;
        speed_loop_autotune_force_stop_output();
    }

    test_control_mode = mode;
}

void speed_loop_autotune_set_left_pwm_cmd(float value)
{
    test_left_pwm_cmd = speed_loop_autotune_clamp_pwm(value);
}

void speed_loop_autotune_set_right_pwm_cmd(float value)
{
    test_right_pwm_cmd = speed_loop_autotune_clamp_pwm(value);
}

void speed_loop_autotune_set_pair_pwm_cmd(float value)
{
    int16 pwm_value;

    pwm_value = speed_loop_autotune_clamp_pwm(value);
    test_left_pwm_cmd = pwm_value;
    test_right_pwm_cmd = pwm_value;
}

uint8 speed_loop_autotune_get_mode_id(void)
{
    if (test_trial_active)
    {
        return AUTOTUNE_TELEMETRY_MODE_GROUND_DUAL;
    }
    if (test_trial_armed)
    {
        return AUTOTUNE_TELEMETRY_MODE_GROUND_DUAL;
    }
    if (test_trial_cooldown_elapsed_ms < test_trial_cooldown_ms)
    {
        return AUTOTUNE_TELEMETRY_MODE_GROUND_DUAL;
    }
    if (test_control_mode == AUTOTUNE_TEST_MODE_PWM_IDENTIFY)
    {
        return AUTOTUNE_TELEMETRY_MODE_PWM_IDENTIFY;
    }
    return AUTOTUNE_TELEMETRY_MODE_AIR_DUAL;
}
