#include "zf_common_headfile.h"
#include "host_service.h"
#include "speed_loop_trial.h"
#include "../../service/vofa.h"

void speed_loop_autotune_service(void)
{
    static char vofa_cmd[64];
    static uint8 last_telemetry_tick = 0;
    uint8 telemetry_tick_now;

    vofa_parse_from_fifo();

    while (vofa_get_command(vofa_cmd, 64))
    {
        handle_vofa_command(vofa_cmd);
    }

    telemetry_tick_now = test_tick_5ms_count;
    if ((uint8)(telemetry_tick_now - last_telemetry_tick) >= 4)
    {
        printf("%f,%f,%f,%ld,%ld,%d,%d\n",
               test_speed_value,
               PID.left_speed.speed,
               PID.right_speed.speed,
               (long)test_left_pwm_output,
               (long)test_right_pwm_output,
               (int)test_trial_active,
               (int)stop);
        last_telemetry_tick = telemetry_tick_now;
    }
}
