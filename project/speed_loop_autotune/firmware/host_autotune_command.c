#include "zf_common_headfile.h"
#include "host_autotune_command.h"
#include "speed_loop_trial.h"
#include "../../service/vofa.h"

#define HOST_AUTOTUNE_VERBOSE_ACK 0

#if HOST_AUTOTUNE_VERBOSE_ACK
#define SPEED_LOOP_AUTOTUNE_PRINT_FLOAT_ACK(name, value) printf("%s = %.2f\n", name, value)
#define SPEED_LOOP_AUTOTUNE_PRINT_UINT_ACK(name, value) printf("%s = %u\n", name, (unsigned int)(value))
#define SPEED_LOOP_AUTOTUNE_PRINT_INT_ACK(name, value) printf("%s = %d\n", name, value)
#define SPEED_LOOP_AUTOTUNE_PRINT_CMD_ACK(name) printf("%s\n", name)
#else
#define SPEED_LOOP_AUTOTUNE_PRINT_FLOAT_ACK(name, value)
#define SPEED_LOOP_AUTOTUNE_PRINT_UINT_ACK(name, value)
#define SPEED_LOOP_AUTOTUNE_PRINT_INT_ACK(name, value)
#define SPEED_LOOP_AUTOTUNE_PRINT_CMD_ACK(name)
#endif

uint8 speed_loop_autotune_handle_param(const char *param_name, float value)
{
    if (strcmp(param_name, "AT_KP") == 0)
    {
        PID.left_speed.Kp = value;
        PID.right_speed.Kp = value;
        SPEED_LOOP_AUTOTUNE_PRINT_FLOAT_ACK("AT_KP", value);
        return 1;
    }
    else if (strcmp(param_name, "AT_KI") == 0)
    {
        PID.left_speed.Ki = value;
        PID.right_speed.Ki = value;
        SPEED_LOOP_AUTOTUNE_PRINT_FLOAT_ACK("AT_KI", value);
        return 1;
    }
    else if (strcmp(param_name, "AT_KD") == 0)
    {
        PID.left_speed.Kd = value;
        PID.right_speed.Kd = value;
        SPEED_LOOP_AUTOTUNE_PRINT_FLOAT_ACK("AT_KD", value);
        return 1;
    }
    else if (strcmp(param_name, "TEST_speed") == 0)
    {
        test_speed_value = value;
        SPEED_LOOP_AUTOTUNE_PRINT_FLOAT_ACK("TEST_speed", value);
        return 1;
    }
    else if (strcmp(param_name, "AT_SPEED") == 0)
    {
        test_speed_value = value;
        SPEED_LOOP_AUTOTUNE_PRINT_FLOAT_ACK("AT_SPEED", value);
        return 1;
    }
    else if (strcmp(param_name, "AT_FUYA") == 0)
    {
        if (value < 0.0f)
        {
            value = 0.0f;
        }
        if (value > 4000.0f)
        {
            value = 4000.0f;
        }
        test_fuya_pwm = (int16)value;
        SPEED_LOOP_AUTOTUNE_PRINT_INT_ACK("AT_FUYA", (int)test_fuya_pwm);
        return 1;
    }
    else if (strcmp(param_name, "AT_TRIAL_MS") == 0)
    {
        if (value < 5.0f)
        {
            value = 5.0f;
        }
        test_trial_limit_ms = (uint16)value;
        SPEED_LOOP_AUTOTUNE_PRINT_UINT_ACK("AT_TRIAL_MS", test_trial_limit_ms);
        return 1;
    }
    else if (strcmp(param_name, "AT_COOLDOWN_MS") == 0)
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
        SPEED_LOOP_AUTOTUNE_PRINT_UINT_ACK("AT_COOLDOWN_MS", test_trial_cooldown_ms);
        return 1;
    }

    return 0;
}

uint8 speed_loop_autotune_handle_command(const char *cmd)
{
    if (strcmp(cmd, "START") == 0)
    {
        stop = 0;
        flat_statr = 3;
        SPEED_LOOP_AUTOTUNE_PRINT_CMD_ACK("START");
        return 1;
    }
    else if (strcmp(cmd, "STOP") == 0)
    {
        ground_load_test_stop();
        SPEED_LOOP_AUTOTUNE_PRINT_CMD_ACK("STOP");
        return 1;
    }
    else if (strcmp(cmd, "AT_ARM") == 0)
    {
        ground_load_test_arm();
        SPEED_LOOP_AUTOTUNE_PRINT_CMD_ACK("AT_ARM");
        return 1;
    }
    else if (strcmp(cmd, "AT_FIRE") == 0)
    {
        ground_load_test_fire();
        SPEED_LOOP_AUTOTUNE_PRINT_CMD_ACK("AT_FIRE");
        return 1;
    }
    else if (strcmp(cmd, "AT_RESET") == 0)
    {
        ground_load_test_stop();
        SPEED_LOOP_AUTOTUNE_PRINT_CMD_ACK("AT_RESET");
        return 1;
    }
    else if (strcmp(cmd, "INFO") == 0)
    {
        speed_loop_autotune_print_info();
        return 1;
    }

    return 0;
}

void speed_loop_autotune_print_info(void)
{
    printf("=== Current Parameters ===\n");
    printf("Left PID_Direction: %.2f, %.2f, %.2f\n",
           PID.left_speed.Kp,
           PID.left_speed.Ki,
           PID.left_speed.Kd);
    printf("Right PID_Direction: %.2f, %.2f, %.2f\n",
           PID.right_speed.Kp,
           PID.right_speed.Ki,
           PID.right_speed.Kd);
    printf("Speed: %.2f\n", test_speed_value);
    printf("Trial: %u, %u, %d, %d\n",
           (unsigned int)test_trial_limit_ms,
           (unsigned int)test_trial_cooldown_ms,
           (int)test_trial_armed,
           (int)test_trial_active);
    printf("Fuya: %d\n", (int)test_fuya_pwm);
    printf("Parser: %lu, %lu\n",
           (unsigned long)vofa_get_frame_overflow_count(),
           (unsigned long)vofa_get_queue_overflow_count());
}
