#ifndef _AUTOTUNE_RUNTIME_H_
#define _AUTOTUNE_RUNTIME_H_

#include "zf_common_typedef.h"

#define AUTOTUNE_TELEMETRY_MODE_AIR_DUAL 0
#define AUTOTUNE_TELEMETRY_MODE_GROUND_DUAL 1
#define AUTOTUNE_TELEMETRY_MODE_PWM_IDENTIFY 2

#define AUTOTUNE_TEST_MODE_SPEED 0
#define AUTOTUNE_TEST_MODE_PWM_IDENTIFY 1

/*
 * 调参组件内部时基（ms），需与 TM0 实际周期保持一致。
 * 当前工程 TM0 周期为 2ms。
 */
#define AUTOTUNE_TEST_TICK_MS 2

extern volatile uint8 test_trial_active;
extern volatile uint8 test_trial_armed;
extern volatile uint16 test_trial_elapsed_ms;
extern volatile uint16 test_trial_limit_ms;
extern volatile uint16 test_trial_cooldown_elapsed_ms;
extern volatile uint16 test_trial_cooldown_ms;
extern volatile int16 test_fuya_pwm;
extern volatile int32 test_left_pwm_output;
extern volatile int32 test_right_pwm_output;
extern volatile int16 test_left_pwm_cmd;
extern volatile int16 test_right_pwm_cmd;
extern volatile uint8 test_tick_5ms_count;
extern volatile uint8 test_control_mode;

void speed_loop_autotune_force_stop_output(void);
void speed_loop_autotune_reset_runtime(uint8 keep_armed, uint8 start_cooldown);
void speed_loop_autotune_start_drive(void);
void speed_loop_autotune_set_test_mode(uint8 mode);
void speed_loop_autotune_set_left_pwm_cmd(float value);
void speed_loop_autotune_set_right_pwm_cmd(float value);
void speed_loop_autotune_set_pair_pwm_cmd(float value);
uint8 speed_loop_autotune_get_mode_id(void);

#endif /* _AUTOTUNE_RUNTIME_H_ */
