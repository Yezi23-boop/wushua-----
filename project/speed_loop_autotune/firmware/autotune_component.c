#include "zf_common_headfile.h"
#include "speed_loop_autotune_private.h"
#include "autotune_pid_core.h"

typedef struct
{
    speed_loop_autotune_binding_t binding;
    const speed_loop_autotune_port_t *port;
    speed_loop_autotune_pid_state_t left_pid;
    speed_loop_autotune_pid_state_t right_pid;
    uint8 initialized;
    uint8 ready;
    uint8 port_ready;
    uint8 missing_binding_mask;
    float target_speed;
    float left_speed;
    float right_speed;
    int32 left_output;
    int32 right_output;
} speed_loop_autotune_component_state_t;

static speed_loop_autotune_component_state_t speed_loop_autotune_component;

static void speed_loop_autotune_component_refresh_ready_state(void)
{
    speed_loop_autotune_component.port_ready = speed_loop_autotune_port_check(speed_loop_autotune_component.port);
    speed_loop_autotune_component.missing_binding_mask =
        speed_loop_autotune_binding_missing_mask(&speed_loop_autotune_component.binding);
    speed_loop_autotune_component.ready =
        (uint8)(speed_loop_autotune_component.port_ready &&
                speed_loop_autotune_component.missing_binding_mask == 0u);
}

static void speed_loop_autotune_component_sync_from_binding(void)
{
    speed_loop_autotune_pid_set_gain(
        &speed_loop_autotune_component.left_pid,
        speed_loop_autotune_binding_read(&speed_loop_autotune_component.binding, SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KP),
        speed_loop_autotune_binding_read(&speed_loop_autotune_component.binding, SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KI),
        speed_loop_autotune_binding_read(&speed_loop_autotune_component.binding, SPEED_LOOP_AUTOTUNE_GAIN_LEFT_KD));
    speed_loop_autotune_pid_set_gain(
        &speed_loop_autotune_component.right_pid,
        speed_loop_autotune_binding_read(&speed_loop_autotune_component.binding, SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KP),
        speed_loop_autotune_binding_read(&speed_loop_autotune_component.binding, SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KI),
        speed_loop_autotune_binding_read(&speed_loop_autotune_component.binding, SPEED_LOOP_AUTOTUNE_GAIN_RIGHT_KD));
}

void speed_loop_autotune_component_init(
    const speed_loop_autotune_binding_t *binding,
    const speed_loop_autotune_port_t *port)
{
    speed_loop_autotune_binding_init(&speed_loop_autotune_component.binding);
    if (binding != 0)
    {
        speed_loop_autotune_component.binding = *binding;
    }

    speed_loop_autotune_component.port = port;
    speed_loop_autotune_component.target_speed = 0.0f;
    speed_loop_autotune_component.left_speed = 0.0f;
    speed_loop_autotune_component.right_speed = 0.0f;
    speed_loop_autotune_component.left_output = 0;
    speed_loop_autotune_component.right_output = 0;
    speed_loop_autotune_pid_init(&speed_loop_autotune_component.left_pid, 0.0f, 0.0f, 0.0f, (float)PWM_DUTY_MAX, (float)PWM_DUTY_MAX);
    speed_loop_autotune_pid_init(&speed_loop_autotune_component.right_pid, 0.0f, 0.0f, 0.0f, (float)PWM_DUTY_MAX, (float)PWM_DUTY_MAX);
    speed_loop_autotune_component_refresh_ready_state();
    speed_loop_autotune_component_sync_from_binding();
    speed_loop_autotune_component.initialized = 1;
}

uint8 speed_loop_autotune_component_is_initialized(void)
{
    return speed_loop_autotune_component.initialized;
}

uint8 speed_loop_autotune_component_is_ready(void)
{
    if (!speed_loop_autotune_component.initialized)
    {
        return 0;
    }
    return speed_loop_autotune_component.ready;
}

uint8 speed_loop_autotune_component_has_valid_port(void)
{
    if (!speed_loop_autotune_component.initialized)
    {
        return 0;
    }
    return speed_loop_autotune_component.port_ready;
}

uint8 speed_loop_autotune_component_get_missing_binding_mask(void)
{
    if (!speed_loop_autotune_component.initialized)
    {
        return (uint8)0xFFu;
    }
    return speed_loop_autotune_component.missing_binding_mask;
}

uint8 speed_loop_autotune_component_set_gain(uint8 slot, float value)
{
    if (!speed_loop_autotune_component.initialized)
    {
        return 0;
    }

    if (!speed_loop_autotune_binding_write(&speed_loop_autotune_component.binding, slot, value))
    {
        speed_loop_autotune_component_refresh_ready_state();
        return 0;
    }

    speed_loop_autotune_component_sync_from_binding();
    speed_loop_autotune_component_refresh_ready_state();
    return 1;
}

float speed_loop_autotune_component_get_gain(uint8 slot)
{
    if (!speed_loop_autotune_component.initialized)
    {
        return 0.0f;
    }
    return speed_loop_autotune_binding_read(&speed_loop_autotune_component.binding, slot);
}

void speed_loop_autotune_component_set_target_speed(float value)
{
    speed_loop_autotune_component.target_speed = value;
}

float speed_loop_autotune_component_get_target_speed(void)
{
    return speed_loop_autotune_component.target_speed;
}

void speed_loop_autotune_component_force_stop_output(void)
{
    speed_loop_autotune_component.left_output = 0;
    speed_loop_autotune_component.right_output = 0;
    if (speed_loop_autotune_component.initialized && speed_loop_autotune_component.port_ready)
    {
        speed_loop_autotune_component.port->force_stop();
    }
}

void speed_loop_autotune_component_reset_control(void)
{
    if (!speed_loop_autotune_component.initialized)
    {
        speed_loop_autotune_component.target_speed = 0.0f;
        speed_loop_autotune_component.left_speed = 0.0f;
        speed_loop_autotune_component.right_speed = 0.0f;
        speed_loop_autotune_component.left_output = 0;
        speed_loop_autotune_component.right_output = 0;
        return;
    }

    speed_loop_autotune_component_sync_from_binding();
    speed_loop_autotune_pid_reset(&speed_loop_autotune_component.left_pid);
    speed_loop_autotune_pid_reset(&speed_loop_autotune_component.right_pid);
    speed_loop_autotune_component.target_speed = 0.0f;
    speed_loop_autotune_component.left_speed = 0.0f;
    speed_loop_autotune_component.right_speed = 0.0f;
    speed_loop_autotune_component_force_stop_output();
}

void speed_loop_autotune_component_set_drive_enabled(uint8 enable, int flat_state)
{
    if (speed_loop_autotune_component.initialized && speed_loop_autotune_component.port_ready)
    {
        speed_loop_autotune_component.port->set_drive_state(enable, flat_state);
    }
}

void speed_loop_autotune_component_write_fuya(int16 pwm_value)
{
    if (speed_loop_autotune_component.initialized && speed_loop_autotune_component.port_ready)
    {
        speed_loop_autotune_component.port->write_fuya_pwm(pwm_value);
    }
}

void speed_loop_autotune_component_run_closed_loop(void)
{
    if (!speed_loop_autotune_component.initialized || !speed_loop_autotune_component.ready)
    {
        speed_loop_autotune_component_force_stop_output();
        return;
    }

    speed_loop_autotune_component_sync_from_binding();
    speed_loop_autotune_component.port->read_speed(
        &speed_loop_autotune_component.left_speed,
        &speed_loop_autotune_component.right_speed);
    speed_loop_autotune_pid_step(
        &speed_loop_autotune_component.left_pid,
        speed_loop_autotune_component.target_speed,
        speed_loop_autotune_component.left_speed);
    speed_loop_autotune_pid_step(
        &speed_loop_autotune_component.right_pid,
        speed_loop_autotune_component.target_speed,
        speed_loop_autotune_component.right_speed);

    if (speed_loop_autotune_component.port->read_stop_flag())
    {
        speed_loop_autotune_component_force_stop_output();
        return;
    }

    speed_loop_autotune_component.left_output =
        (int32)speed_loop_autotune_pid_get_output(&speed_loop_autotune_component.left_pid);
    speed_loop_autotune_component.right_output =
        (int32)speed_loop_autotune_pid_get_output(&speed_loop_autotune_component.right_pid);
    speed_loop_autotune_component.port->write_motor_pwm(
        speed_loop_autotune_component.left_output,
        speed_loop_autotune_component.right_output);
}

void speed_loop_autotune_component_sample_feedback(void)
{
    if (!speed_loop_autotune_component.initialized || !speed_loop_autotune_component.port_ready)
    {
        speed_loop_autotune_component.left_speed = 0.0f;
        speed_loop_autotune_component.right_speed = 0.0f;
        return;
    }

    speed_loop_autotune_component.port->read_speed(
        &speed_loop_autotune_component.left_speed,
        &speed_loop_autotune_component.right_speed);
}

void speed_loop_autotune_component_run_open_loop_pwm(int16 left_pwm, int16 right_pwm)
{
    if (!speed_loop_autotune_component.initialized || !speed_loop_autotune_component.ready)
    {
        speed_loop_autotune_component_force_stop_output();
        return;
    }

    speed_loop_autotune_component_sample_feedback();
    if (speed_loop_autotune_component.port->read_stop_flag())
    {
        speed_loop_autotune_component_force_stop_output();
        return;
    }

    speed_loop_autotune_component.left_output = (int32)left_pwm;
    speed_loop_autotune_component.right_output = (int32)right_pwm;
    speed_loop_autotune_component.port->write_motor_pwm(
        speed_loop_autotune_component.left_output,
        speed_loop_autotune_component.right_output);
}

uint8 speed_loop_autotune_component_get_stop_flag(void)
{
    if (!speed_loop_autotune_component.initialized || !speed_loop_autotune_component.port_ready)
    {
        return 1;
    }
    return speed_loop_autotune_component.port->read_stop_flag();
}

float speed_loop_autotune_component_get_left_speed(void)
{
    return speed_loop_autotune_component.left_speed;
}

float speed_loop_autotune_component_get_right_speed(void)
{
    return speed_loop_autotune_component.right_speed;
}

int32 speed_loop_autotune_component_get_left_output(void)
{
    return speed_loop_autotune_component.left_output;
}

int32 speed_loop_autotune_component_get_right_output(void)
{
    return speed_loop_autotune_component.right_output;
}
