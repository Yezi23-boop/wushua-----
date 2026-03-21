#include "zf_common_headfile.h"
#include "speed_loop_trial.h"

#define TEST_TICK_MS 5

volatile uint8 test_trial_active = 0;
volatile uint8 test_trial_armed = 0;
volatile uint16 test_trial_elapsed_ms = 0;
volatile uint16 test_trial_limit_ms = 250;
volatile uint16 test_trial_cooldown_elapsed_ms = 450;
volatile uint16 test_trial_cooldown_ms = 450;
volatile int16 test_fuya_pwm = 2000;
volatile int32 test_left_pwm_output = 0;
volatile int32 test_right_pwm_output = 0;
volatile uint8 test_tick_5ms_count = 0;

static void ground_load_force_stop_output(void);
static void ground_load_apply_stop(uint8 keep_armed, uint8 start_cooldown);
static uint8 ground_load_mode_enabled(void);

static void ground_load_force_stop_output(void)
{
    pwm_set_duty(PWMB_CH2_P13, 0);
    pwm_set_duty(PWMB_CH3_P52, 0);
}

static void ground_load_apply_stop(uint8 keep_armed, uint8 start_cooldown)
{
    stop = 1;
    flat_statr = 0;
    test_trial_active = 0;
    test_trial_elapsed_ms = 0;

    if (!keep_armed)
    {
        test_trial_armed = 0;
    }

    pid_speed_reset(&PID.left_speed);
    pid_speed_reset(&PID.right_speed);
    test_speed_value = 0.0f;
    test_left_pwm_output = 0;
    test_right_pwm_output = 0;
    ground_load_force_stop_output();

    if (start_cooldown)
    {
        test_trial_cooldown_elapsed_ms = 0;
    }
    else
    {
        test_trial_cooldown_elapsed_ms = test_trial_cooldown_ms;
    }
}

static uint8 ground_load_mode_enabled(void)
{
    if (test_trial_active)
    {
        return 1;
    }

    if (test_trial_armed)
    {
        return 1;
    }

    if (test_trial_cooldown_elapsed_ms < test_trial_cooldown_ms)
    {
        return 1;
    }

    return 0;
}

void ground_load_test_arm(void)
{
    test_trial_armed = 1;
    ground_load_apply_stop(1, 0);
    test_trial_armed = 1;
}

void ground_load_test_fire(void)
{
    if (!test_trial_armed)
    {
        return;
    }

    stop = 0;
    flat_statr = 3;
    test_trial_active = 1;
    test_trial_elapsed_ms = 0;
    test_trial_cooldown_elapsed_ms = 0;
    pid_speed_reset(&PID.left_speed);
    pid_speed_reset(&PID.right_speed);
    test_left_pwm_output = 0;
    test_right_pwm_output = 0;
    ground_load_force_stop_output();
}

void ground_load_test_stop(void)
{
    ground_load_apply_stop(0, 0);
    fuya_motor_output(0);
}

void run_test_speed(void)
{
    a_run_apply_iap_guard();
    test_tick_5ms_count++;

    if (ground_load_mode_enabled())
    {
        if (test_fuya_pwm > 0)
        {
            fuya_motor_output((int)test_fuya_pwm);
        }
        else
        {
            fuya_motor_output(0);
        }

        if (test_trial_active)
        {
            test_speed_func();
            test_left_pwm_output = (int32)PID.left_speed.output;
            test_right_pwm_output = (int32)PID.right_speed.output;
            test_trial_elapsed_ms += TEST_TICK_MS;

            if (test_trial_elapsed_ms >= test_trial_limit_ms)
            {
                ground_load_apply_stop(1, 1);
            }
        }
        else
        {
            Encoder_get(&PID.left_speed, &PID.right_speed);
            test_left_pwm_output = 0;
            test_right_pwm_output = 0;
            ground_load_force_stop_output();

            if (test_trial_cooldown_elapsed_ms < test_trial_cooldown_ms)
            {
                test_trial_cooldown_elapsed_ms += TEST_TICK_MS;
                if (test_trial_cooldown_elapsed_ms > test_trial_cooldown_ms)
                {
                    test_trial_cooldown_elapsed_ms = test_trial_cooldown_ms;
                }
            }
        }
        return;
    }

    test_speed_func();
    test_left_pwm_output = (int32)PID.left_speed.output;
    test_right_pwm_output = (int32)PID.right_speed.output;
}
