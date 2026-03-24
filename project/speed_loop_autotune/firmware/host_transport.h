#ifndef _HOST_TRANSPORT_H_
#define _HOST_TRANSPORT_H_

#include "zf_common_typedef.h"

uint8 speed_loop_autotune_handle_text_command(char *cmd);
void speed_loop_autotune_emit_telemetry(void);
void speed_loop_autotune_set_parser_stats(uint32 frame_overflow_count, uint32 queue_overflow_count);
uint32 speed_loop_autotune_get_parser_frame_overflow_count(void);
uint32 speed_loop_autotune_get_parser_queue_overflow_count(void);

#endif /* _HOST_TRANSPORT_H_ */
