/**
 * @file vofa.c
 * @brief VOFA+ 串口通信与指令解析协议栈
 * @details
 * 提供无线串口 (USART1/2/3/4) 在线联机调试协议：向上位机按规定格式发送浮点遥测，
 * 并支持异步解析基于字节流指令下发的调参动作。
 *
 * 功能包含：
 * 1) 从无线串口 FIFO 按字节解析命令帧（'!' 结尾）；
 * 2) 使用固定深度环形队列缓存完整命令，避免主循环瞬时堵塞；
 * 3) 将命令转发到 legacy 参数解析路径；
 * 4) 统计帧溢出和队列溢出次数，供上位机诊断链路质量。
 */
#include "zf_common_headfile.h"
#include "vofa.h"
#include <stdlib.h>

static vofa_data_struct vofa_data;

static void vofa_parse_byte(uint8 dat);
static void vofa_enqueue_command(const uint8 *cmd, uint8 len);
static void vofa_handle_legacy_command(char *cmd);

/**
 * @brief VOFA 解析器初始化
 * @details
 * 在开机 init_user 时调用，清空解析缓存与命令队列，重置状态统计。
 * 保证首次串口接收状态不被遗留垃圾数据干扰。
 */
void vofa_init(void)
{
    uint8 i;
    uint8 j;

    /* 清空当前拼帧缓冲 */
    for (i = 0; i < VOFA_BUFFER_SIZE; i++)
    {
        vofa_data.buffer[i] = 0;
    }

    /* 清空命令队列与各槽位长度 */
    for (i = 0; i < VOFA_CMD_QUEUE_DEPTH; i++)
    {
        vofa_data.cmd_queue_len[i] = 0;
        for (j = 0; j < VOFA_MAX_CMD_LEN; j++)
        {
            vofa_data.cmd_queue[i][j] = 0;
        }
    }

    vofa_data.index = 0;
    vofa_data.cmd_queue_head = 0;
    vofa_data.cmd_queue_tail = 0;
    vofa_data.cmd_queue_count = 0;
    vofa_data.discard_frame = 0;
    vofa_data.frame_overflow_count = 0;
    vofa_data.queue_overflow_count = 0;
    vofa_data.state = VOFA_PARSE_IDLE;
}

/**
 * @brief 从无线串口 FIFO 取数并触发帧解析
 * @details
 * 通常在主循环的 debug_vofa_service() 中调用，防止因耗时过长导致控制环中断延迟。
 */
void vofa_parse_from_fifo(void)
{
    uint8 dat;

    while (wireless_uart_read_buffer(&dat, 1) > 0)
    {
        vofa_parse_byte(dat);
    }
}

/**
 * @brief 单字节解析状态机
 * @details
 * - 以 '!' 为帧结束符界定指令长度；
 * - 遇到超长帧则进入 discard 模式，直到遇到下一个结束符，丢弃整个异常帧；
 * - 正常解析完成后，加尾零入队，将读取负荷与后续解析解耦。
 */
static void vofa_parse_byte(uint8 dat)
{
    /* 帧结束符：提交当前帧或结束 discard 模式 */
    if (dat == '!' || dat == 0x21)
    {
        if (vofa_data.discard_frame)
        {
            /* 丢弃状态下遇到结束符，表示本帧彻底跳过并复位 */
            vofa_data.index = 0;
            vofa_data.discard_frame = 0;
            vofa_data.state = (vofa_data.cmd_queue_count > 0) ? VOFA_PARSE_COMPLETE : VOFA_PARSE_IDLE;
        }
        else if (vofa_data.index > 0)
        {
            /* 正常帧结束：补尾零并入队 */
            vofa_data.buffer[vofa_data.index] = '\0';
            vofa_enqueue_command(vofa_data.buffer, vofa_data.index);
            vofa_data.index = 0;
        }
    }
    else
    {
        if (vofa_data.discard_frame)
        {
            /* discard 模式中持续丢弃，直到遇到结束符 */
            return;
        }

        /* 预留 1 字节给字符串结束符，避免越界 */
        if (vofa_data.index < (VOFA_MAX_CMD_LEN - 1) && vofa_data.index < (VOFA_BUFFER_SIZE - 1))
        {
            vofa_data.buffer[vofa_data.index++] = dat;
            vofa_data.state = VOFA_PARSE_RECEIVING;
        }
        else
        {
            /* 超长帧：切到丢弃模式并统计一次 frame overflow */
            vofa_data.index = 0;
            vofa_data.discard_frame = 1;
            vofa_data.frame_overflow_count++;
            vofa_data.state = VOFA_PARSE_RECEIVING;
        }
    }
}

/**
 * @brief 将完整命令入环形缓冲队列
 * @details
 * 队列深度(VOFA_CMD_QUEUE_DEPTH)上限时则直接丢包，避免串口指令洪塞引起系统 OOM 或死锁。
 * 单包长度通过宏固定，内存静态分配无越界风险。
 */
static void vofa_enqueue_command(const uint8 *cmd, uint8 len)
{
    uint8 tail;

    /* 队列满则直接丢包，不阻塞主循环 */
    if (vofa_data.cmd_queue_count >= VOFA_CMD_QUEUE_DEPTH)
    {
        vofa_data.queue_overflow_count++;
        return;
    }

    /* 写入尾槽位并推进尾指针 */
    tail = vofa_data.cmd_queue_tail;
    memcpy(vofa_data.cmd_queue[tail], cmd, len);
    vofa_data.cmd_queue[tail][len] = '\0';
    vofa_data.cmd_queue_len[tail] = len;

    vofa_data.cmd_queue_tail++;
    if (vofa_data.cmd_queue_tail >= VOFA_CMD_QUEUE_DEPTH)
    {
        vofa_data.cmd_queue_tail = 0;
    }

    vofa_data.cmd_queue_count++;
    vofa_data.state = VOFA_PARSE_COMPLETE;
}

/**
 * @brief 读取一条待处理命令
 * @param cmd_out 输出缓冲区
 * @param max_len 输出缓冲区长度
 * @return 1 读取成功；0 队列为空
 */
uint8 vofa_get_command(char *cmd_out, uint8 max_len)
{
    uint8 head;
    uint8 cmd_len;
    uint8 copy_len;

    /* 队列为空直接返回，避免无效内存访问 */
    if (vofa_data.cmd_queue_count == 0)
    {
        return 0;
    }

    head = vofa_data.cmd_queue_head;
    cmd_len = vofa_data.cmd_queue_len[head];
    copy_len = 0;

    if (max_len > 0)
    {
        /* 拷贝时保留结尾 '\0'，防止输出缓冲越界 */
        copy_len = cmd_len;
        if (copy_len >= max_len)
        {
            copy_len = max_len - 1;
        }
        memcpy(cmd_out, vofa_data.cmd_queue[head], copy_len);
        cmd_out[copy_len] = '\0';
    }

    /* 消费头槽位并推进读指针 */
    vofa_data.cmd_queue_len[head] = 0;
    vofa_data.cmd_queue_head++;
    if (vofa_data.cmd_queue_head >= VOFA_CMD_QUEUE_DEPTH)
    {
        vofa_data.cmd_queue_head = 0;
    }

    vofa_data.cmd_queue_count--;
    vofa_data.state = (vofa_data.cmd_queue_count > 0) ? VOFA_PARSE_COMPLETE : VOFA_PARSE_IDLE;

    return 1;
}

/**
 * @brief 清空解析状态与命令队列
 */
void vofa_clear_buffer(void)
{
    vofa_data.index = 0;
    vofa_data.cmd_queue_head = 0;
    vofa_data.cmd_queue_tail = 0;
    vofa_data.cmd_queue_count = 0;
    vofa_data.discard_frame = 0;
    vofa_data.state = VOFA_PARSE_IDLE;
}

/**
 * @brief 获取帧溢出计数
 */
uint32 vofa_get_frame_overflow_count(void)
{
    return vofa_data.frame_overflow_count;
}

/**
 * @brief 获取命令队列溢出计数
 */
uint32 vofa_get_queue_overflow_count(void)
{
    return vofa_data.queue_overflow_count;
}

/**
 * @brief VOFA 主服务
 * @details
 * 解析输入命令并交给统一命令处理入口。自动调参固件移除后，
 * 本入口保留为主循环稳定调用点，内部回落到 legacy 指令路径。
 */
void vofa_service(void)
{
    static char vofa_cmd[64];

    /* 拉取并解析串口数据 */
    vofa_parse_from_fifo();

    /* 逐条消费命令，避免单次阻塞过久 */
    while (vofa_get_command(vofa_cmd, 64))
    {
        handle_vofa_command(vofa_cmd);
    }
}

/**
 * @brief VOFA 兼容服务（旧版命令模式）
 */
void vofa_service_legacy(void)
{
    static char vofa_cmd[32];
#if MAIN_ENABLE_ISR_TEST_SPEED_FUNC
    printf("%f,%f,%f,%f,%f\n", PID.left_speed.speed, PID.right_speed.speed, test_speed_value, PID.left_speed.Kp, PID.left_speed.Ki);
#endif
#if MAIN_ENABLE_ISR_TEST_DIFF_FUNC
    printf("%f,%f,%f,%f,%f\n", PID.left_speed.speed, PID.right_speed.speed, test_speed_value, PID.left_speed.Kp, PID.left_speed.Ki);
#endif
    // printf("%f,%f,%f,%f,%f\n", PID.left_speed.speed, PID.right_speed.speed, test_speed_value,PID.left_speed.Kp,PID.left_speed.Ki);
    /* legacy 模式下只做旧命令兼容，不走新调参组件 */
    vofa_parse_from_fifo();

    while (vofa_get_command(vofa_cmd, 32))
    {
        vofa_handle_legacy_command(vofa_cmd);
        //     printf("%f,%f,,%f,,%f,%f\n", PID.left_speed.speed, PID.right_speed.speed, test_speed_value,PID.left_speed.Kp,PID.left_speed.Ki);
    }
}

/**
 * @brief 旧版文本命令解析
 * @details 支持手工调 PID、保存/加载参数等历史命令。
 */
static void vofa_handle_legacy_command(char *cmd)
{
    char *eq_pos;
    char param_name[16];
    uint8 name_len;
    float value;
    uint8 i;

    /* 先清空临时参数名缓存，避免脏数据影响匹配 */
    for (i = 0; i < 16; i++)
    {
        param_name[i] = 0;
    }

    eq_pos = strchr(cmd, '=');

    if (eq_pos != NULL)
    {
        /* key=value 类型命令：先拆分参数名，再解析数值 */
        name_len = (uint8)(eq_pos - cmd);

        if (name_len < 16)
        {
            memcpy(param_name, cmd, name_len);
            param_name[name_len] = '\0';
            value = atof(eq_pos + 1);

            if (strcmp(param_name, "L_KP") == 0)
            {
                /* 速度环左轮参数在线更新 */
                PID.left_speed.Kp = value;
            }
            else if (strcmp(param_name, "L_KI") == 0)
            {
                PID.left_speed.Ki = value;
            }
            else if (strcmp(param_name, "L_KD") == 0)
            {
                PID.left_speed.Kd = value;
            }
            else if (strcmp(param_name, "R_KP") == 0)
            {
                PID.right_speed.Kp = value;
            }
            else if (strcmp(param_name, "R_KI") == 0)
            {
                PID.right_speed.Ki = value;
            }
            else if (strcmp(param_name, "R_KD") == 0)
            {
                PID.right_speed.Kd = value;
            }
            else if (strcmp(param_name, "TEST_DIFF") == 0)
            {
                test_diff_value = value;
            }
            else if (strcmp(param_name, "TEST_speed") == 0)
            {
                test_speed_value = value;
            }
            else if (strcmp(param_name, "MOTOR") == 0)
            {
                PID.left_speed.output = value;
                PID.right_speed.output = value;
            }
            else if (strcmp(param_name, "SPEED_RUN") == 0)
            {
            }
            else if (strcmp(param_name, "ERR") == 0)
            {
                Err = value;
            }
        }
    }
    else
    {
        /* 无等号命令：按动作类指令处理 */
        if (strcmp(cmd, "FUYA") == 0)
        {
        }
        else if (strcmp(cmd, "SAVE") == 0)
        {
            eeprom_flash();
        }
        else if (strcmp(cmd, "LOAD") == 0)
        {
            eeprom_init();
        }
        else if (strcmp(cmd, "STOP") == 0)
        {
            stop = 1;
        }
        else if (strcmp(cmd, "START") == 0)
        {
            stop = 0;
        }
    }
}

/**
 * @brief VOFA 命令统一分发入口
 * @details 自动调参固件移除后，仅保留 legacy 参数解析路径。
 */
void handle_vofa_command(char *cmd)
{
    vofa_handle_legacy_command(cmd);
}
