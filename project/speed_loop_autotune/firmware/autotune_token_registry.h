#ifndef _AUTOTUNE_TOKEN_REGISTRY_H_
#define _AUTOTUNE_TOKEN_REGISTRY_H_

#include "zf_common_typedef.h"

typedef uint8 (*speed_loop_autotune_param_handler_t)(float value);
typedef uint8 (*speed_loop_autotune_command_handler_t)(void);

typedef struct
{
    const char *name;
    speed_loop_autotune_param_handler_t handler;
} speed_loop_autotune_param_registration_t;

typedef struct
{
    const char *name;
    speed_loop_autotune_command_handler_t handler;
} speed_loop_autotune_command_registration_t;

uint8 speed_loop_autotune_dispatch_param(const char *param_name, float value);
uint8 speed_loop_autotune_dispatch_command(const char *cmd);
const speed_loop_autotune_param_registration_t *speed_loop_autotune_get_param_registry(uint8 *count);
const speed_loop_autotune_command_registration_t *speed_loop_autotune_get_command_registry(uint8 *count);

#endif /* _AUTOTUNE_TOKEN_REGISTRY_H_ */
