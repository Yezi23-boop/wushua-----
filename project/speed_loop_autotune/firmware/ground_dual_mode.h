#ifndef _GROUND_DUAL_MODE_H_
#define _GROUND_DUAL_MODE_H_

#include "zf_common_typedef.h"

uint8 ground_dual_set_speed(float value);
uint8 ground_dual_set_fuya(float value);
uint8 ground_dual_set_trial_ms(float value);
uint8 ground_dual_set_cooldown_ms(float value);
uint8 ground_dual_command_arm(void);
uint8 ground_dual_command_fire(void);
void ground_dual_run_tick(void);

#endif /* _GROUND_DUAL_MODE_H_ */
