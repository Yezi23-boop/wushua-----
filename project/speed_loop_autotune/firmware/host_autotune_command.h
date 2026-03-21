#ifndef _HOST_AUTOTUNE_COMMAND_H_
#define _HOST_AUTOTUNE_COMMAND_H_

#include "zf_common_typedef.h"

uint8 speed_loop_autotune_handle_param(const char *param_name, float value);
uint8 speed_loop_autotune_handle_command(const char *cmd);
void speed_loop_autotune_print_info(void);

#endif /* _HOST_AUTOTUNE_COMMAND_H_ */
