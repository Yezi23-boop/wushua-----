#include "zf_common_headfile.h"
#include "autotune_pid_core.h"

void speed_loop_autotune_pid_init(
    speed_loop_autotune_pid_state_t *pid,
    float kp,
    float ki,
    float kd,
    float max_out,
    float min_out)
{
    if (pid == 0)
    {
        return;
    }

    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
    pid->error = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev2_error = 0.0f;
    pid->output = 0.0f;
    pid->max_output = max_out;
    pid->min_output = min_out;
    pid->speed = 0.0f;
}

void speed_loop_autotune_pid_reset(speed_loop_autotune_pid_state_t *pid)
{
    if (pid == 0)
    {
        return;
    }

    pid->error = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev2_error = 0.0f;
    pid->output = 0.0f;
    pid->speed = 0.0f;
}

void speed_loop_autotune_pid_set_gain(
    speed_loop_autotune_pid_state_t *pid,
    float kp,
    float ki,
    float kd)
{
    if (pid == 0)
    {
        return;
    }

    pid->Kp = kp;
    pid->Ki = ki;
    pid->Kd = kd;
}

void speed_loop_autotune_pid_step(speed_loop_autotune_pid_state_t *pid, float target, float actual)
{
    float delta_output;

    if (pid == 0)
    {
        return;
    }

    pid->speed = actual;
    pid->error = target - actual;

    delta_output = pid->Kp * (pid->error - pid->prev_error) +
                   pid->Ki * pid->error +
                   pid->Kd * (pid->error - 2.0f * pid->prev_error + pid->prev2_error);

    pid->output += delta_output;

    if (pid->output > pid->max_output)
    {
        pid->output = pid->max_output;
    }
    else if (pid->output < -pid->max_output)
    {
        pid->output = -pid->max_output;
    }

    pid->prev2_error = pid->prev_error;
    pid->prev_error = pid->error;
}

float speed_loop_autotune_pid_get_output(const speed_loop_autotune_pid_state_t *pid)
{
    if (pid == 0)
    {
        return 0.0f;
    }
    return pid->output;
}
