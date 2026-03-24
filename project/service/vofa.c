#include "zf_common_headfile.h"
#include "vofa.h"
#include "../speed_loop_autotune/firmware/host_transport.h"
#include <stdlib.h>

static vofa_data_struct vofa_data;

static void vofa_parse_byte(uint8 dat);
static void vofa_enqueue_command(const uint8 *cmd, uint8 len);
static void vofa_handle_legacy_command(char *cmd);

void vofa_init(void)
{
    uint8 i;
    uint8 j;

    for (i = 0; i < VOFA_BUFFER_SIZE; i++)
    {
        vofa_data.buffer[i] = 0;
    }

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

void vofa_parse_from_fifo(void)
{
    uint8 dat;

    while (wireless_uart_read_buffer(&dat, 1) > 0)
    {
        vofa_parse_byte(dat);
    }
}

static void vofa_parse_byte(uint8 dat)
{
    if (dat == '!' || dat == 0x21)
    {
        if (vofa_data.discard_frame)
        {
            vofa_data.index = 0;
            vofa_data.discard_frame = 0;
            vofa_data.state = (vofa_data.cmd_queue_count > 0) ? VOFA_PARSE_COMPLETE : VOFA_PARSE_IDLE;
        }
        else if (vofa_data.index > 0)
        {
            vofa_data.buffer[vofa_data.index] = '\0';
            vofa_enqueue_command(vofa_data.buffer, vofa_data.index);
            vofa_data.index = 0;
        }
    }
    else
    {
        if (vofa_data.discard_frame)
        {
            return;
        }

        if (vofa_data.index < (VOFA_MAX_CMD_LEN - 1) && vofa_data.index < (VOFA_BUFFER_SIZE - 1))
        {
            vofa_data.buffer[vofa_data.index++] = dat;
            vofa_data.state = VOFA_PARSE_RECEIVING;
        }
        else
        {
            vofa_data.index = 0;
            vofa_data.discard_frame = 1;
            vofa_data.frame_overflow_count++;
            vofa_data.state = VOFA_PARSE_RECEIVING;
        }
    }
}

static void vofa_enqueue_command(const uint8 *cmd, uint8 len)
{
    uint8 tail;

    if (vofa_data.cmd_queue_count >= VOFA_CMD_QUEUE_DEPTH)
    {
        vofa_data.queue_overflow_count++;
        return;
    }

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

uint8 vofa_get_command(char *cmd_out, uint8 max_len)
{
    uint8 head;
    uint8 cmd_len;
    uint8 copy_len;

    if (vofa_data.cmd_queue_count == 0)
    {
        return 0;
    }

    head = vofa_data.cmd_queue_head;
    cmd_len = vofa_data.cmd_queue_len[head];
    copy_len = 0;

    if (max_len > 0)
    {
        copy_len = cmd_len;
        if (copy_len >= max_len)
        {
            copy_len = max_len - 1;
        }
        memcpy(cmd_out, vofa_data.cmd_queue[head], copy_len);
        cmd_out[copy_len] = '\0';
    }

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

void vofa_clear_buffer(void)
{
    vofa_data.index = 0;
    vofa_data.cmd_queue_head = 0;
    vofa_data.cmd_queue_tail = 0;
    vofa_data.cmd_queue_count = 0;
    vofa_data.discard_frame = 0;
    vofa_data.state = VOFA_PARSE_IDLE;
}

uint32 vofa_get_frame_overflow_count(void)
{
    return vofa_data.frame_overflow_count;
}

uint32 vofa_get_queue_overflow_count(void)
{
    return vofa_data.queue_overflow_count;
}

void vofa_service(void)
{
    static char vofa_cmd[64];

    vofa_parse_from_fifo();
    speed_loop_autotune_set_parser_stats(
        vofa_get_frame_overflow_count(),
        vofa_get_queue_overflow_count());

    while (vofa_get_command(vofa_cmd, 64))
    {
        handle_vofa_command(vofa_cmd);
    }

    speed_loop_autotune_emit_telemetry();
}

void vofa_service_legacy(void)
{
    static char vofa_cmd[64];

    vofa_parse_from_fifo();

    while (vofa_get_command(vofa_cmd, 64))
    {
        vofa_handle_legacy_command(vofa_cmd);
    }
}

static void vofa_handle_legacy_command(char *cmd)
{
    char *eq_pos;
    char param_name[16];
    uint8 name_len;
    float value;
    uint8 i;

    for (i = 0; i < 16; i++)
    {
        param_name[i] = 0;
    }

    eq_pos = strchr(cmd, '=');

    if (eq_pos != NULL)
    {
        name_len = (uint8)(eq_pos - cmd);

        if (name_len < 16)
        {
            memcpy(param_name, cmd, name_len);
            param_name[name_len] = '\0';
            value = atof(eq_pos + 1);

            if (strcmp(param_name, "L_KP") == 0)
            {
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
            else if (strcmp(param_name, "A_KP") == 0)
            {
                PID.angle.Kp = value;
                printf("Position Kp = %.2f\n", value);
            }
            else if (strcmp(param_name, "A_KD") == 0)
            {
                PID.angle.Kd = value;
                printf("Position Kd = %.2f\n", value);
            }
            else if (strcmp(param_name, "A_GYRO") == 0)
            {
                PID.angle.Kp2 = value;
                printf("Position limiting_Err (from Ki) = %.2f\n", value);
            }
            else if (strcmp(param_name, "TEST_angle") == 0)
            {
                test_angle_value = value;
                printf("TEST_angle = %f\n", value);
            }
            else if (strcmp(param_name, "MOTOR") == 0)
            {
                PID.left_speed.output = value;
                PID.right_speed.output = value;
                printf("Motor = %.2f,speed=%.2f\n", PID.right_speed.output, PID.right_speed.speed);
            }
            else if (strcmp(param_name, "SPEED_RUN") == 0)
            {
                printf("Speed Run = %.2f\n", value);
            }
            else if (strcmp(param_name, "ERR") == 0)
            {
                Err = value;
                printf("Error = %.2f\n", value);
            }
            else
            {
                printf("Unknown parameter: %s = %.2f\n", param_name, value);
            }
        }
    }
    else
    {
        if (strcmp(cmd, "FUYA") == 0)
        {
        }
        else if (strcmp(cmd, "SAVE") == 0)
        {
            eeprom_flash();
            printf("Parameters saved\n");
        }
        else if (strcmp(cmd, "LOAD") == 0)
        {
            eeprom_init();
            printf("Parameters loaded\n");
        }
        else
        {
            printf("Unknown command: %s\n", cmd);
        }
    }
}

void handle_vofa_command(char *cmd)
{
    if (speed_loop_autotune_handle_text_command(cmd))
    {
        return;
    }

    vofa_handle_legacy_command(cmd);
}
