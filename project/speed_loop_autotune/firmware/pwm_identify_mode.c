#include "zf_common_headfile.h"
#include "pwm_identify_mode.h"
#include "speed_loop_autotune_private.h"
#include "autotune_runtime.h"

uint8 pwm_identify_set_test_mode(float value)
{
    if (value >= 0.5f)
    {
        speed_loop_autotune_set_test_mode(AUTOTUNE_TEST_MODE_PWM_IDENTIFY);
    }
    else
    {
        speed_loop_autotune_set_test_mode(AUTOTUNE_TEST_MODE_SPEED);
    }
    return 1;
}

uint8 pwm_identify_set_left_pwm(float value)
{
    speed_loop_autotune_set_left_pwm_cmd(value);
    return 1;
}

uint8 pwm_identify_set_right_pwm(float value)
{
    speed_loop_autotune_set_right_pwm_cmd(value);
    return 1;
}

uint8 pwm_identify_set_pair_pwm(float value)
{
    speed_loop_autotune_set_pair_pwm_cmd(value);
    return 1;
}

void pwm_identify_run_tick(void)
{
    speed_loop_autotune_component_run_open_loop_pwm(test_left_pwm_cmd, test_right_pwm_cmd);
    test_left_pwm_output = speed_loop_autotune_component_get_left_output();
    test_right_pwm_output = speed_loop_autotune_component_get_right_output();
}
