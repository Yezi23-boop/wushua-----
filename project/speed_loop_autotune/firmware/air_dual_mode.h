#ifndef _AIR_DUAL_MODE_H_
#define _AIR_DUAL_MODE_H_

#include "zf_common_typedef.h"

uint8 air_dual_set_shared_kp(float value);
uint8 air_dual_set_shared_ki(float value);
uint8 air_dual_set_shared_kd(float value);
uint8 air_dual_set_test_speed(float value);
void air_dual_run_tick(void);

#endif /* _AIR_DUAL_MODE_H_ */
