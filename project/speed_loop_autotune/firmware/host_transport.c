#include "zf_common_headfile.h"
#include "host_transport.h"
#include "host_autotune_command.h"
#include "host_service.h"

static uint32 speed_loop_autotune_parser_frame_overflow_count = 0;
static uint32 speed_loop_autotune_parser_queue_overflow_count = 0;

uint8 speed_loop_autotune_handle_text_command(char *cmd)
{
    return speed_loop_autotune_handle_command_text(cmd);
}

void speed_loop_autotune_emit_telemetry(void)
{
    speed_loop_autotune_service();
}

void speed_loop_autotune_set_parser_stats(uint32 frame_overflow_count, uint32 queue_overflow_count)
{
    speed_loop_autotune_parser_frame_overflow_count = frame_overflow_count;
    speed_loop_autotune_parser_queue_overflow_count = queue_overflow_count;
}

uint32 speed_loop_autotune_get_parser_frame_overflow_count(void)
{
    return speed_loop_autotune_parser_frame_overflow_count;
}

uint32 speed_loop_autotune_get_parser_queue_overflow_count(void)
{
    return speed_loop_autotune_parser_queue_overflow_count;
}
