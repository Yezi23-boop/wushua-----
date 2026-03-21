#ifndef _SPEED_LOOP_TRIAL_H_
#define _SPEED_LOOP_TRIAL_H_

#include "zf_common_typedef.h"

extern volatile uint8 test_trial_active;
extern volatile uint8 test_trial_armed;
extern volatile uint16 test_trial_elapsed_ms;
extern volatile uint16 test_trial_limit_ms;
extern volatile uint16 test_trial_cooldown_elapsed_ms;
extern volatile uint16 test_trial_cooldown_ms;
extern volatile int16 test_fuya_pwm;
extern volatile int32 test_left_pwm_output;
extern volatile int32 test_right_pwm_output;
extern volatile uint8 test_tick_5ms_count;

void run_test_speed(void);
void ground_load_test_arm(void);
void ground_load_test_fire(void);
void ground_load_test_stop(void);

#endif /* _SPEED_LOOP_TRIAL_H_ */
