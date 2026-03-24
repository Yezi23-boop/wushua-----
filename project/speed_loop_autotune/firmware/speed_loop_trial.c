#include "zf_common_headfile.h"
#include "speed_loop_trial.h"
#include "speed_loop_autotune_private.h"
#include "autotune_runtime.h"
#include "air_dual_mode.h"
#include "ground_dual_mode.h"
#include "pwm_identify_mode.h"

void run_test_speed(void)
{
    uint8 mode_id;

    a_run_apply_iap_guard();
    test_tick_5ms_count++;

    if (!speed_loop_autotune_component_is_initialized())
    {
        speed_loop_autotune_force_stop_output();
        return;
    }

    mode_id = speed_loop_autotune_get_mode_id();

    if (mode_id == AUTOTUNE_TELEMETRY_MODE_GROUND_DUAL)
    {
        ground_dual_run_tick();
        return;
    }
    if (mode_id == AUTOTUNE_TELEMETRY_MODE_PWM_IDENTIFY)
    {
        pwm_identify_run_tick();
        return;
    }
    air_dual_run_tick();
}
