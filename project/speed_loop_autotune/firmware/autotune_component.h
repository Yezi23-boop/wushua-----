#ifndef _AUTOTUNE_COMPONENT_H_
#define _AUTOTUNE_COMPONENT_H_

#include "speed_loop_autotune_private.h"

uint8 speed_loop_autotune_component_is_initialized(void);
uint8 speed_loop_autotune_component_is_ready(void);
uint8 speed_loop_autotune_component_has_valid_port(void);
uint8 speed_loop_autotune_component_get_missing_binding_mask(void);

uint8 speed_loop_autotune_component_set_gain(uint8 slot, float value);
float speed_loop_autotune_component_get_gain(uint8 slot);
void speed_loop_autotune_component_set_target_speed(float value);
float speed_loop_autotune_component_get_target_speed(void);

void speed_loop_autotune_component_reset_control(void);
void speed_loop_autotune_component_force_stop_output(void);
void speed_loop_autotune_component_set_drive_enabled(uint8 enable, int flat_state);
void speed_loop_autotune_component_write_fuya(int16 pwm_value);

void speed_loop_autotune_component_run_closed_loop(void);
void speed_loop_autotune_component_sample_feedback(void);
void speed_loop_autotune_component_run_open_loop_pwm(int16 left_pwm, int16 right_pwm);

uint8 speed_loop_autotune_component_get_stop_flag(void);
float speed_loop_autotune_component_get_left_speed(void);
float speed_loop_autotune_component_get_right_speed(void);
int32 speed_loop_autotune_component_get_left_output(void);
int32 speed_loop_autotune_component_get_right_output(void);

#endif /* _AUTOTUNE_COMPONENT_H_ */
