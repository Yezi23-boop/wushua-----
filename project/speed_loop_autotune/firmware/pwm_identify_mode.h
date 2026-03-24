#ifndef _PWM_IDENTIFY_MODE_H_
#define _PWM_IDENTIFY_MODE_H_

#include "zf_common_typedef.h"

uint8 pwm_identify_set_test_mode(float value);
uint8 pwm_identify_set_left_pwm(float value);
uint8 pwm_identify_set_right_pwm(float value);
uint8 pwm_identify_set_pair_pwm(float value);
void pwm_identify_run_tick(void);

#endif /* _PWM_IDENTIFY_MODE_H_ */
