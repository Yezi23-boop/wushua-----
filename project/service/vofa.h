#ifndef _VOFA_H_
#define _VOFA_H_

#include "zf_common_typedef.h"

#define VOFA_BUFFER_SIZE 64
#define VOFA_MAX_CMD_LEN 32
#define VOFA_CMD_QUEUE_DEPTH 8

typedef enum
{
    VOFA_PARSE_IDLE = 0,
    VOFA_PARSE_RECEIVING,
    VOFA_PARSE_COMPLETE
} vofa_parse_state_enum;

typedef struct
{
    uint8 buffer[VOFA_BUFFER_SIZE];
    uint8 cmd_queue[VOFA_CMD_QUEUE_DEPTH][VOFA_MAX_CMD_LEN];
    uint8 cmd_queue_len[VOFA_CMD_QUEUE_DEPTH];
    uint8 index;
    uint8 cmd_queue_head;
    uint8 cmd_queue_tail;
    uint8 cmd_queue_count;
    uint8 discard_frame;
    uint32 frame_overflow_count;
    uint32 queue_overflow_count;
    vofa_parse_state_enum state;
} vofa_data_struct;

void vofa_init(void);
void vofa_parse_from_fifo(void);
uint8 vofa_get_command(char *cmd_out, uint8 max_len);
void vofa_clear_buffer(void);
void vofa_parse_command(char *cmd);
void handle_vofa_command(char *cmd);
uint32 vofa_get_frame_overflow_count(void);
uint32 vofa_get_queue_overflow_count(void);

#endif
