#ifndef _VOFA_H_
#define _VOFA_H_

#include "zf_common_typedef.h"

/**
 * @file vofa.h
 * @brief VOFA 串口命令解析接口
 * @details
 * 定义 VOFA 命令解析状态、队列缓存结构和对外服务函数，
 * 供上位机调参与在线诊断链路调用。
 */

#define VOFA_BUFFER_SIZE 64    /* 原始接收缓冲区大小 */
#define VOFA_MAX_CMD_LEN 32    /* 单条命令最大长度（含结束前有效字符） */
#define VOFA_CMD_QUEUE_DEPTH 8 /* 命令环形队列深度 */

typedef enum
{
    VOFA_PARSE_IDLE = 0,  /* 空闲状态，等待新命令 */
    VOFA_PARSE_RECEIVING, /* 正在接收一条命令 */
    VOFA_PARSE_COMPLETE   /* 至少有一条完整命令待处理 */
} vofa_parse_state_enum;

typedef struct
{
    uint8 buffer[VOFA_BUFFER_SIZE];                          /* 当前正在拼接的命令缓存 */
    uint8 cmd_queue[VOFA_CMD_QUEUE_DEPTH][VOFA_MAX_CMD_LEN]; /* 命令环形队列 */
    uint8 cmd_queue_len[VOFA_CMD_QUEUE_DEPTH];               /* 各队列槽位命令长度 */
    uint8 index;                                             /* 当前拼接写入位置 */
    uint8 cmd_queue_head;                                    /* 队列读指针 */
    uint8 cmd_queue_tail;                                    /* 队列写指针 */
    uint8 cmd_queue_count;                                   /* 队列内待处理命令数量 */
    uint8 discard_frame;                                     /* 超长帧丢弃标志 */
    uint32 frame_overflow_count;                             /* 超长帧计数 */
    uint32 queue_overflow_count;                             /* 队列满丢包计数 */
    vofa_parse_state_enum state;                             /* 当前解析状态 */
} vofa_data_struct;

/* 初始化与解析 */
void vofa_init(void);
void vofa_parse_from_fifo(void);
uint8 vofa_get_command(char *cmd_out, uint8 max_len);
void vofa_clear_buffer(void);

/* 服务入口 */
void vofa_service(void);
void vofa_service_legacy(void);
void handle_vofa_command(char *cmd);

/* 诊断统计 */
uint32 vofa_get_frame_overflow_count(void);
uint32 vofa_get_queue_overflow_count(void);

#endif
