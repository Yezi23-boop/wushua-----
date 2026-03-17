#ifndef _VOFA_H_
#define _VOFA_H_

#include "zf_common_typedef.h"

/**
 * @brief VOFA+ FireWater 协议缓冲区配置
 */
#define VOFA_BUFFER_SIZE 64 /**< 接收原始数据缓冲区大小 */
#define VOFA_MAX_CMD_LEN 32 /**< 解析后的完整指令最大长度 */

/**
 * @brief VOFA 解析状态机枚举
 */
typedef enum
{
    VOFA_PARSE_IDLE = 0,  /**< 空闲状态，等待起始数据 */
    VOFA_PARSE_RECEIVING, /**< 正在接收指令内容 */
    VOFA_PARSE_COMPLETE   /**< 检测到帧尾标识，解析完成 */
} vofa_parse_state_enum;

/**
 * @brief VOFA 数据管理结构体
 */
typedef struct
{
    uint8 buffer[VOFA_BUFFER_SIZE];     /**< 接收原始字节缓冲区 */
    uint8 cmd_buffer[VOFA_MAX_CMD_LEN]; /**< 存放解析完成的命令字符串 */
    uint8 index;                        /**< 当前缓冲区写入索引 */
    uint8 cmd_len;                      /**< 当前已解析出的命令长度 */
    vofa_parse_state_enum state;        /**< 解析状态机当前状态 */
} vofa_data_struct;

/* --- 函数声明 --- */

/**
 * @brief 初始化 VOFA+ 协议解析器
 */
void vofa_init(void);

/**
 * @brief 从串口 FIFO 缓冲区拉取并解析数据
 * @details 应在主循环中高频调用，以防 FIFO 溢出
 */
void vofa_parse_from_fifo(void);

/**
 * @brief 获取解析完成的完整命令
 * @param cmd_out 输出：存储命令的缓冲区
 * @param max_len 输入：输出缓冲区的最大容量
 * @return 1 表示有新命令，0 表示无新命令
 */
uint8 vofa_get_command(char *cmd_out, uint8 max_len);

/**
 * @brief 强制清空解析缓冲区
 */
void vofa_clear_buffer(void);

/**
 * @brief 处理获取到的命令串
 * @details 负责解析 "KP=1.2" 或 "START" 等格式的控制指令
 */
void handle_vofa_command(char *cmd);

#endif /* _VOFA_H_ */
