#include "zf_common_headfile.h"
#include "speed_loop_autotune_private.h"
#include "host_service.h"
#include "speed_loop_trial.h"

/* 保持串口遥测约 20ms 一帧，避免上位机采样假设失配。 */
#define AUTOTUNE_TELEMETRY_PERIOD_MS 20
#define AUTOTUNE_TELEMETRY_TICK_DIV ((AUTOTUNE_TELEMETRY_PERIOD_MS + AUTOTUNE_TEST_TICK_MS - 1) / AUTOTUNE_TEST_TICK_MS)

void speed_loop_autotune_service(void)
{
    static uint8 last_telemetry_tick = 0;
    uint8 telemetry_tick_now;

    telemetry_tick_now = test_tick_5ms_count;
    if ((uint8)(telemetry_tick_now - last_telemetry_tick) >= (uint8)AUTOTUNE_TELEMETRY_TICK_DIV)
    {
        printf("%f,%f,%f,%ld,%ld,%d,%d,%d,%d,%d,%u,%u,%u\n",
               speed_loop_autotune_component_get_target_speed(),
               speed_loop_autotune_component_get_left_speed(),
               speed_loop_autotune_component_get_right_speed(),
               (long)speed_loop_autotune_component_get_left_output(),
               (long)speed_loop_autotune_component_get_right_output(),
               (int)test_trial_active,
               (int)speed_loop_autotune_component_get_stop_flag(),
               (int)speed_loop_autotune_get_mode_id(),
               (int)test_left_pwm_cmd,
               (int)test_right_pwm_cmd,
               (unsigned int)test_start_seq_cmd,
               (unsigned int)test_start_seq_latched,
               (unsigned int)test_start_state);
        last_telemetry_tick = telemetry_tick_now;
    }
}
