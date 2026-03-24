#ifndef _AUTOTUNE_PID_CORE_H_
#define _AUTOTUNE_PID_CORE_H_

#include "zf_common_typedef.h"

typedef struct
{
    float Kp;
    float Ki;
    float Kd;
    float error;
    float prev_error;
    float prev2_error;
    float output;
    float max_output;
    float min_output;
    float speed;
} speed_loop_autotune_pid_state_t;

void speed_loop_autotune_pid_init(
    speed_loop_autotune_pid_state_t *pid,
    float kp,
    float ki,
    float kd,
    float max_out,
    float min_out);
void speed_loop_autotune_pid_reset(speed_loop_autotune_pid_state_t *pid);
void speed_loop_autotune_pid_set_gain(
    speed_loop_autotune_pid_state_t *pid,
    float kp,
    float ki,
    float kd);
void speed_loop_autotune_pid_step(speed_loop_autotune_pid_state_t *pid, float target, float actual);
float speed_loop_autotune_pid_get_output(const speed_loop_autotune_pid_state_t *pid);

#endif /* _AUTOTUNE_PID_CORE_H_ */
