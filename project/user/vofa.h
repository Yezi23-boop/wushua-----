#ifndef _VOFA_H_
#define _VOFA_H_

#include "zf_common_typedef.h"

// VOFA+ FireWater 协议缓冲区定义
#define VOFA_BUFFER_SIZE 64 // 接收缓冲区大小，从64增大到256
#define VOFA_MAX_CMD_LEN 32  // 最大命令长度，从32增大到128

// 解析状态
typedef enum
{
    VOFA_PARSE_IDLE = 0,  // 空闲状态
    VOFA_PARSE_RECEIVING, // 正在接收
    VOFA_PARSE_COMPLETE   // 解析完成
} vofa_parse_state_enum;

// VOFA 数据结构
typedef struct
{
    uint8 buffer[VOFA_BUFFER_SIZE];     // 接收缓冲区
    uint8 cmd_buffer[VOFA_MAX_CMD_LEN]; // 命令缓冲区
    uint8 index;                        // 当前索引
    uint8 cmd_len;                      // 命令长度
    vofa_parse_state_enum state;        // 解析状态
} vofa_data_struct;

// 函数声明
void vofa_init(void);
void vofa_parse_from_fifo(void); // 从 FIFO 读取数据并解析（在循环中调用）
uint8 vofa_get_command(char *cmd_out, uint8 max_len);
void vofa_clear_buffer(void);
void vofa_parse_command(char *cmd);
void handle_vofa_command(char *cmd);

#endif // _VOFA_H_
