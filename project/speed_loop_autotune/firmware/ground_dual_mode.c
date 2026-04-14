#include "zf_common_headfile.h"
#include "ground_dual_mode.h"
#include "speed_loop_autotune_private.h"
#include "autotune_runtime.h"

uint8 ground_dual_command_arm(void)
{
    test_trial_armed = 1;
    speed_loop_autotune_reset_runtime(1, 0);
    test_trial_armed = 1;
    return 1;
}

uint8 ground_dual_command_fire(void)
{
    if (!test_trial_armed)
    {
        return 1;
    }

    speed_loop_autotune_start_drive();
    test_trial_active = 1;
    test_trial_elapsed_ms = 0;
    test_trial_cooldown_elapsed_ms = 0;
    speed_loop_autotune_component_reset_control();
    test_left_pwm_output = 0;
    test_right_pwm_output = 0;
    speed_loop_autotune_force_stop_output();
    return 1;
}

uint8 ground_dual_set_speed(float value)
{
    speed_loop_autotune_component_set_target_speed(value);
    return 1;
}

uint8 ground_dual_set_fuya(float value)
{
    if (value < 0.0f)
    {
        value = 0.0f;
    }
    if (value > 100.0f)
    {
        value = 100.0f;
    }
    test_fuya_pwm = (int16)value;
    return 1;
}

uint8 ground_dual_set_trial_ms(float value)
{
    if (value < 5.0f)
    {
        value = 5.0f;
    }
    test_trial_limit_ms = (uint16)value;
    return 1;
}

uint8 ground_dual_set_cooldown_ms(float value)
{
    if (value < 5.0f)
    {
        value = 5.0f;
    }
    test_trial_cooldown_ms = (uint16)value;
    if (!test_trial_active && !test_trial_armed)
    {
        test_trial_cooldown_elapsed_ms = test_trial_cooldown_ms;
    }
    else if (test_trial_cooldown_elapsed_ms > test_trial_cooldown_ms)
    {
        test_trial_cooldown_elapsed_ms = test_trial_cooldown_ms;
    }
    return 1;
}

void ground_dual_run_tick(void)
{
    speed_loop_autotune_component_write_fuya(test_fuya_pwm);

    if (test_trial_active)
    {
        speed_loop_autotune_component_run_closed_loop();
        speed_loop_autotune_mark_drive_running();
        test_left_pwm_output = speed_loop_autotune_component_get_left_output();
        test_right_pwm_output = speed_loop_autotune_component_get_right_output();
        test_trial_elapsed_ms += AUTOTUNE_TEST_TICK_MS;

        if (test_trial_elapsed_ms >= test_trial_limit_ms)
        {
            speed_loop_autotune_reset_runtime(1, 1);
        }
    }
    else
    {
        speed_loop_autotune_component_sample_feedback();
        test_left_pwm_output = 0;
        test_right_pwm_output = 0;
        speed_loop_autotune_force_stop_output();

        if (test_trial_cooldown_elapsed_ms < test_trial_cooldown_ms)
        {
            test_trial_cooldown_elapsed_ms += AUTOTUNE_TEST_TICK_MS;
            if (test_trial_cooldown_elapsed_ms > test_trial_cooldown_ms)
            {
                test_trial_cooldown_elapsed_ms = test_trial_cooldown_ms;
            }
        }
    }
}
