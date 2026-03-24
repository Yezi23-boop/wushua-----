#include "zf_common_headfile.h"
#include "air_dual_mode.h"
#include "speed_loop_autotune_private.h"
#include "autotune_runtime.h"

uint8 air_dual_set_shared_kp(float value)
{
    uint8 left_ok;
    uint8 right_ok;

    left_ok = speed_loop_autotune_component_set_gain(SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KP, value);
    right_ok = speed_loop_autotune_component_set_gain(SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KP, value);
    return (uint8)(left_ok && right_ok);
}

uint8 air_dual_set_shared_ki(float value)
{
    uint8 left_ok;
    uint8 right_ok;

    left_ok = speed_loop_autotune_component_set_gain(SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KI, value);
    right_ok = speed_loop_autotune_component_set_gain(SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KI, value);
    return (uint8)(left_ok && right_ok);
}

uint8 air_dual_set_shared_kd(float value)
{
    uint8 left_ok;
    uint8 right_ok;

    left_ok = speed_loop_autotune_component_set_gain(SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KD, value);
    right_ok = speed_loop_autotune_component_set_gain(SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KD, value);
    return (uint8)(left_ok && right_ok);
}

uint8 air_dual_set_test_speed(float value)
{
    speed_loop_autotune_component_set_target_speed(value);
    return 1;
}

void air_dual_run_tick(void)
{
    speed_loop_autotune_component_run_closed_loop();
    test_left_pwm_output = speed_loop_autotune_component_get_left_output();
    test_right_pwm_output = speed_loop_autotune_component_get_right_output();
}
