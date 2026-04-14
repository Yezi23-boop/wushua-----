#include "zf_common_headfile.h"
#include "host_autotune_command.h"
#include "speed_loop_autotune_private.h"
#include "host_transport.h"
#include "autotune_runtime.h"
#include "autotune_token_registry.h"
#include <stdlib.h>

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
    if (speed_loop_autotune_dispatch_param(param_name, value))
    {
        SPEED_LOOP_AUTOTUNE_PRINT_FLOAT_ACK(param_name, value);
        return 1;
    }
    return 0;
}

uint8 speed_loop_autotune_handle_command(const char *cmd)
{
    if (speed_loop_autotune_dispatch_command(cmd))
    {
        SPEED_LOOP_AUTOTUNE_PRINT_CMD_ACK(cmd);
        return 1;
    }
    return 0;
}

uint8 speed_loop_autotune_handle_command_text(char *cmd)
{
    char *eq_pos;
    char param_name[16];
    uint8 name_len;
    float value;
    uint8 i;

    for (i = 0; i < 16; i++)
    {
        param_name[i] = 0;
    }

    eq_pos = strchr(cmd, '=');

    if (eq_pos != NULL)
    {
        name_len = (uint8)(eq_pos - cmd);

        if (name_len < 16)
        {
            memcpy(param_name, cmd, name_len);
            param_name[name_len] = '\0';
            value = atof(eq_pos + 1);
            return speed_loop_autotune_handle_param(param_name, value);
        }
        return 0;
    }

    return speed_loop_autotune_handle_command(cmd);
}

void speed_loop_autotune_print_info(void)
{
    uint8 slot;
    uint8 missing_mask;

    printf("=== Current Parameters ===\n");
    printf("Left PID_Direction: %.2f, %.2f, %.2f\n",
           speed_loop_autotune_component_get_gain(SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KP),
           speed_loop_autotune_component_get_gain(SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KI),
           speed_loop_autotune_component_get_gain(SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KD));
    printf("Right PID_Direction: %.2f, %.2f, %.2f\n",
           speed_loop_autotune_component_get_gain(SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KP),
           speed_loop_autotune_component_get_gain(SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KI),
           speed_loop_autotune_component_get_gain(SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KD));
    printf("Mode: %u, %u\n",
           (unsigned int)speed_loop_autotune_get_mode_id(),
           (unsigned int)test_control_mode);
    printf("Speed: %.2f\n", speed_loop_autotune_component_get_target_speed());
    printf("PWM Cmd: %d, %d\n",
           (int)test_left_pwm_cmd,
           (int)test_right_pwm_cmd);
    printf("Start Session: %u, %u, %u\n",
           (unsigned int)test_start_seq_cmd,
           (unsigned int)test_start_seq_latched,
           (unsigned int)test_start_state);
    printf("Trial: %u, %u, %d, %d\n",
           (unsigned int)test_trial_limit_ms,
           (unsigned int)test_trial_cooldown_ms,
           (int)test_trial_armed,
           (int)test_trial_active);
    printf("Fuya Percent: %d\n", (int)test_fuya_pwm);
    printf("Binding: %u, %u\n",
           (unsigned int)speed_loop_autotune_component_is_ready(),
           (unsigned int)speed_loop_autotune_component_has_valid_port());
    missing_mask = speed_loop_autotune_component_get_missing_binding_mask();
    if (missing_mask != 0u)
    {
        for (slot = 0; slot < SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT; slot++)
        {
            if ((missing_mask & (uint8)(1u << slot)) != 0u)
            {
                printf("Missing Binding: %s\n", speed_loop_autotune_binding_slot_name(slot));
            }
        }
    }
    printf("Parser: %lu, %lu\n",
           (unsigned long)speed_loop_autotune_get_parser_frame_overflow_count(),
           (unsigned long)speed_loop_autotune_get_parser_queue_overflow_count());
}
