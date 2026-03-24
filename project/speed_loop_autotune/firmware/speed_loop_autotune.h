#ifndef _SPEED_LOOP_AUTOTUNE_H_
#define _SPEED_LOOP_AUTOTUNE_H_

#include "zf_common_typedef.h"

#define SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KP 0
#define SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KI 1
#define SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KD 2
#define SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KP 3
#define SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KI 4
#define SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KD 5
#define SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT 6

typedef struct
{
    const char *name;
    float *slot_addr;
} speed_loop_autotune_binding_slot_t;

typedef struct
{
    speed_loop_autotune_binding_slot_t slots[SPEED_LOOP_AUTOTUNE_GAIN_SLOT_COUNT];
} speed_loop_autotune_binding_t;

typedef void (*speed_loop_autotune_read_speed_t)(float *left_speed, float *right_speed);
typedef void (*speed_loop_autotune_write_motor_pwm_t)(int32 left_pwm, int32 right_pwm);
typedef void (*speed_loop_autotune_write_fuya_pwm_t)(int16 pwm_value);
typedef void (*speed_loop_autotune_set_drive_state_t)(uint8 enable, int flat_state);
typedef void (*speed_loop_autotune_force_stop_t)(void);
typedef uint8 (*speed_loop_autotune_read_stop_flag_t)(void);

typedef struct
{
    speed_loop_autotune_read_speed_t read_speed;
    speed_loop_autotune_write_motor_pwm_t write_motor_pwm;
    speed_loop_autotune_write_fuya_pwm_t write_fuya_pwm;
    speed_loop_autotune_set_drive_state_t set_drive_state;
    speed_loop_autotune_force_stop_t force_stop;
    speed_loop_autotune_read_stop_flag_t read_stop_flag;
} speed_loop_autotune_port_t;

void speed_loop_autotune_binding_init(speed_loop_autotune_binding_t *binding);
void speed_loop_autotune_binding_bind(
    speed_loop_autotune_binding_t *binding,
    uint8 slot,
    float *slot_addr,
    const char *name);
void speed_loop_autotune_component_init(
    const speed_loop_autotune_binding_t *binding,
    const speed_loop_autotune_port_t *port);

#endif /* _SPEED_LOOP_AUTOTUNE_H_ */
