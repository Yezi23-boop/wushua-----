#ifndef _SPEED_LOOP_AUTOTUNE_PRIVATE_H_
#define _SPEED_LOOP_AUTOTUNE_PRIVATE_H_

#include "speed_loop_autotune.h"

uint8 speed_loop_autotune_binding_check(const speed_loop_autotune_binding_t *binding);
uint8 speed_loop_autotune_binding_missing_mask(const speed_loop_autotune_binding_t *binding);
uint8 speed_loop_autotune_binding_is_bound(const speed_loop_autotune_binding_t *binding, uint8 slot);
float speed_loop_autotune_binding_read(const speed_loop_autotune_binding_t *binding, uint8 slot);
uint8 speed_loop_autotune_binding_write(const speed_loop_autotune_binding_t *binding, uint8 slot, float value);
void speed_loop_autotune_binding_mirror_to_array(
    const speed_loop_autotune_binding_t *binding,
    float *values,
    uint8 count);
void speed_loop_autotune_binding_mirror_from_array(
    const speed_loop_autotune_binding_t *binding,
    const float *values,
    uint8 count);
const char *speed_loop_autotune_binding_slot_name(uint8 slot);

uint8 speed_loop_autotune_port_check(const speed_loop_autotune_port_t *port);

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

#endif /* _SPEED_LOOP_AUTOTUNE_PRIVATE_H_ */
