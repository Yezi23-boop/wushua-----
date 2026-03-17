#include "zf_common_headfile.h"
#include "vofa.h"
#include <stdlib.h>

/* 内部私有 VOFA 数据管理对象 */
static vofa_data_struct vofa_data;

/* 内部私有单字节解析函数声明 */
static void vofa_parse_byte(uint8 dat);

/**
 * @brief 初始化 VOFA 数据结构
 */
void vofa_init(void)
{
    uint8 i;

    /* 初始化缓冲区为全 0 */
    for (i = 0; i < VOFA_BUFFER_SIZE; i++)
        vofa_data.buffer[i] = 0;
    for (i = 0; i < VOFA_MAX_CMD_LEN; i++)
        vofa_data.cmd_buffer[i] = 0;

    vofa_data.index = 0;
    vofa_data.cmd_len = 0;
    vofa_data.state = VOFA_PARSE_IDLE;
}

/**
 * @brief 从无线串口 FIFO 读取数据并送入解析器
 */
void vofa_parse_from_fifo(void)
{
    uint8 dat;
    /* 循环读取无线串口 FIFO 中的所有待处理字节 */
    while (wireless_uart_read_buffer(&dat, 1) > 0)
    {
        vofa_parse_byte(dat);
    }
}

/**
 * @brief 逐字节解析 VOFA+ FireWater 协议
 * @details 协议约定：以 '!' 作为一帧指令的结束符
 * @param dat 待解析的字节
 */
static void vofa_parse_byte(uint8 dat)
{
    /* 检测到帧尾标识 '!' (ASCII: 0x21) */
    if (dat == '!' || dat == 0x21)
    {
        if (vofa_data.index > 0)
        {
            /* 1. 添加字符串结束符，使之符合 C 风格字符串 */
            vofa_data.buffer[vofa_data.index] = '\0';

            /* 2. 拷贝解析出的命令到独立缓冲区，供应用层读取 */
            memcpy(vofa_data.cmd_buffer, vofa_data.buffer, vofa_data.index + 1);
            vofa_data.cmd_len = vofa_data.index;

            /* 3. 更新状态机为完成状态 */
            vofa_data.state = VOFA_PARSE_COMPLETE;

            /* 4. 重置接收索引，准备下一帧 */
            vofa_data.index = 0;
        }
    }
    else
    {
        /* --- 正在接收有效指令内容 --- */
        if (vofa_data.index < VOFA_BUFFER_SIZE - 1)
        {
            vofa_data.buffer[vofa_data.index++] = dat;
            vofa_data.state = VOFA_PARSE_RECEIVING;
        }
        else
        {
            /* 缓冲区溢出保护：清空当前非法接收 */
            vofa_data.index = 0;
            vofa_data.state = VOFA_PARSE_IDLE;
        }
    }
}

/**
 * @brief 获取解析好的命令
 * @details 获取后会自动将状态重置为 IDLE，确保每条指令仅被处理一次
 */
uint8 vofa_get_command(char *cmd_out, uint8 max_len)
{
    uint8 result = 0;

    if (vofa_data.state == VOFA_PARSE_COMPLETE)
    {
        /* 校验长度并执行拷贝 */
        if (vofa_data.cmd_len < max_len)
        {
            memcpy(cmd_out, (char *)vofa_data.cmd_buffer, vofa_data.cmd_len + 1);
            result = 1;
        }

        /* 消费掉当前指令，清空标志位 */
        vofa_data.state = VOFA_PARSE_IDLE;
        vofa_data.cmd_len = 0;
    }

    return result;
}

/**
 * @brief 强制清空缓冲区
 */
void vofa_clear_buffer(void)
{
    vofa_data.index = 0;
    vofa_data.cmd_len = 0;
    vofa_data.state = VOFA_PARSE_IDLE;
}

/**
 * @brief 处理具体的控制指令
 * @details 支持 "PARAM=VALUE" 格式的参数修改和 "COMMAND" 格式的状态控制
 */
void handle_vofa_command(char *cmd)
{
    char *eq_pos;
    char param_name[16];
    uint8 name_len;
    float value;
    uint8 i;

    /* 初始化临时缓冲区 */
    for (i = 0; i < 16; i++)
        param_name[i] = 0;

    /* 查找是否存在等号 '=' */
    eq_pos = strchr(cmd, '=');

    if (eq_pos != NULL)
    {
        /* ========== A. 带参数的赋值指令 (如: L_KP=1.5) ========== */
        name_len = (uint8)(eq_pos - cmd);

        if (name_len < 16)
        {
            /* 提取参数名 */
            memcpy(param_name, cmd, name_len);
            param_name[name_len] = '\0';

            /* 提取数值（将等号后的字符串转换为浮点数） */
            value = (float)atof(eq_pos + 1);

            /* --- 匹配参数名并执行更新 --- */
            if (strcmp(param_name, "L_KP") == 0)
            {
                PID.left_speed.Kp = value;
                printf("L_Kp -> %.2f\n", value);
            }
            else if (strcmp(param_name, "L_KI") == 0)
            {
                PID.left_speed.Ki = value;
                printf("L_Ki -> %.2f\n", value);
            }
            else if (strcmp(param_name, "L_KD") == 0)
            {
                PID.left_speed.Kd = value;
                printf("L_Kd -> %.2f\n", value);
            }
            else if (strcmp(param_name, "R_KP") == 0)
            {
                PID.right_speed.Kp = value;
                printf("R_Kp -> %.2f\n", value);
            }
            else if (strcmp(param_name, "R_KI") == 0)
            {
                PID.right_speed.Ki = value;
                printf("R_Ki -> %.2f\n", value);
            }
            else if (strcmp(param_name, "R_KD") == 0)
            {
                PID.right_speed.Kd = value;
                printf("R_Kd -> %.2f\n", value);
            }
            else if (strcmp(param_name, "A_KP") == 0)
            {
                g_app_config.angle.kp_Angle = value;
                control_apply_config();
                printf("A_Kp -> %.2f\n", value);
            }
            else if (strcmp(param_name, "A_KD") == 0)
            {
                g_app_config.angle.kd_Angle = value;
                control_apply_config();
                printf("A_Kd -> %.2f\n", value);
            }
            else if (strcmp(param_name, "SPEED_RUN") == 0)
            {
                g_app_config.speed.speed_run = value;
                control_apply_config();
                printf("RunSpeed -> %.2f\n", value);
            }
            else if (strcmp(param_name, "TEST_S") == 0)
            {
                test_speed_value = value;
                printf("TestSpeed -> %.2f\n", value);
            }
            else if (strcmp(param_name, "TEST_A") == 0)
            {
                test_angle_value = value;
                printf("TestAngle -> %.2f\n", value);
            }
            else
            {
                printf("Err: Unknown Param [%s]\n", param_name);
            }
        }
    }
    else
    {
        /* ========== B. 独立命令 (如: START, STOP) ========== */
        if (strcmp(cmd, "START") == 0)
        {
            stop = 0;
            flat_statr = 3; /* 解除停车，进入运行准备状态 */
            printf("CMD: START\n");
        }
        else if (strcmp(cmd, "STOP") == 0)
        {
            stop = 1;
            flat_statr = 0; /* 强制停车 */
            printf("CMD: STOP\n");
        }
        else if (strcmp(cmd, "SAVE") == 0)
        {
            config_save(); /* 将当前所有参数保存至 EEPROM */
            printf("CMD: SAVE OK\n");
        }
        else if (strcmp(cmd, "LOAD") == 0)
        {
            config_load(); /* 从 EEPROM 重新加载参数 */
            printf("CMD: LOAD OK\n");
        }
        else
        {
            printf("Err: Unknown Cmd [%s]\n", cmd);
        }
    }
}
