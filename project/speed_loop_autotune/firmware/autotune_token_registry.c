#include "zf_common_headfile.h"
#include "autotune_token_registry.h"
#include "autotune_runtime.h"
#include "host_autotune_command.h"
#include "air_dual_mode.h"
#include "ground_dual_mode.h"
#include "pwm_identify_mode.h"

static uint8 speed_loop_autotune_command_start(void)
{
    speed_loop_autotune_start_drive();
    return 1;
}

static uint8 speed_loop_autotune_command_stop(void)
{
    speed_loop_autotune_reset_runtime(0, 0);
    return 1;
}

static uint8 speed_loop_autotune_command_reset(void)
{
    speed_loop_autotune_reset_runtime(0, 0);
    return 1;
}

static uint8 speed_loop_autotune_command_info(void)
{
    speed_loop_autotune_print_info();
    return 1;
}

static const speed_loop_autotune_param_registration_t speed_loop_autotune_param_registry[] = {
    {"AT_KP", air_dual_set_shared_kp},
    {"AT_KI", air_dual_set_shared_ki},
    {"AT_KD", air_dual_set_shared_kd},
    {"TEST_speed", air_dual_set_test_speed},
    {"AT_SPEED", ground_dual_set_speed},
    {"AT_FUYA", ground_dual_set_fuya},
    {"AT_TRIAL_MS", ground_dual_set_trial_ms},
    {"AT_COOLDOWN_MS", ground_dual_set_cooldown_ms},
    {"AT_TEST_MODE", pwm_identify_set_test_mode},
    {"AT_START_SEQ", pwm_identify_set_start_seq},
    {"L_TEST_PWM", pwm_identify_set_left_pwm},
    {"R_TEST_PWM", pwm_identify_set_right_pwm},
    {"TEST_pwm", pwm_identify_set_pair_pwm},
};

static const speed_loop_autotune_command_registration_t speed_loop_autotune_command_registry[] = {
    {"START", speed_loop_autotune_command_start},
    {"STOP", speed_loop_autotune_command_stop},
    {"AT_RESET", speed_loop_autotune_command_reset},
    {"AT_ARM", ground_dual_command_arm},
    {"AT_FIRE", ground_dual_command_fire},
    {"INFO", speed_loop_autotune_command_info},
};

const speed_loop_autotune_param_registration_t *speed_loop_autotune_get_param_registry(uint8 *count)
{
    if (count != 0)
    {
        *count = (uint8)(sizeof(speed_loop_autotune_param_registry) / sizeof(speed_loop_autotune_param_registry[0]));
    }
    return speed_loop_autotune_param_registry;
}

const speed_loop_autotune_command_registration_t *speed_loop_autotune_get_command_registry(uint8 *count)
{
    if (count != 0)
    {
        *count = (uint8)(sizeof(speed_loop_autotune_command_registry) / sizeof(speed_loop_autotune_command_registry[0]));
    }
    return speed_loop_autotune_command_registry;
}

uint8 speed_loop_autotune_dispatch_param(const char *param_name, float value)
{
    uint8 count;
    uint8 index;
    const speed_loop_autotune_param_registration_t *registry;

    registry = speed_loop_autotune_get_param_registry(&count);
    for (index = 0; index < count; index++)
    {
        if (strcmp(param_name, registry[index].name) == 0)
        {
            return registry[index].handler(value);
        }
    }

    return 0;
}

uint8 speed_loop_autotune_dispatch_command(const char *cmd)
{
    uint8 count;
    uint8 index;
    const speed_loop_autotune_command_registration_t *registry;

    registry = speed_loop_autotune_get_command_registry(&count);
    for (index = 0; index < count; index++)
    {
        if (strcmp(cmd, registry[index].name) == 0)
        {
            return registry[index].handler();
        }
    }

    return 0;
}
