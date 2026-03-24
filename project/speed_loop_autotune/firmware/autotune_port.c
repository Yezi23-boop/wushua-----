#include "zf_common_headfile.h"
#include "speed_loop_autotune_private.h"

uint8 speed_loop_autotune_port_check(const speed_loop_autotune_port_t *port)
{
    if (port == 0)
    {
        return 0;
    }
    if (port->read_speed == 0)
    {
        return 0;
    }
    if (port->write_motor_pwm == 0)
    {
        return 0;
    }
    if (port->write_fuya_pwm == 0)
    {
        return 0;
    }
    if (port->set_drive_state == 0)
    {
        return 0;
    }
    if (port->force_stop == 0)
    {
        return 0;
    }
    if (port->read_stop_flag == 0)
    {
        return 0;
    }
    return 1;
}
