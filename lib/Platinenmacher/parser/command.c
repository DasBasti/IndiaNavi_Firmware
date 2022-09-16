/*
 * command.c
 *
 *  Created on: Jan 16, 2021
 *      Author: bastian
 */

#include "command.h"

char* cmd_buffer;
cmd_t state;
char* cmd;
char* args;
char* cmd_buf;
char* args_buf;

command_t unknown_cmd;
command_t* cmd_list[CMD_MAX];
uint8_t max_cmd;

/*
 * Search the cmd_lsit for the command with name in *cmd
 */
static uint8_t find_command(char* cmd)
{
    uint8_t idx = 0;
    while (cmd_list[idx]) {
        if (strcmp(cmd_list[idx]->command, cmd) == 0)
            return idx;
        idx++;
    }
    return 0; /* command 0 is unknown command */
}

static void run_command(uint8_t idx)
{
    cmd_list[idx]->args = args_buf;
    if (cmd_list[idx]->function_cb)
        cmd_list[idx]->function_cb(cmd_list[idx]);
    cmd = cmd_buf;
    memset(cmd, 0, CMD_LENGTH + 1);
    args = args_buf;
    memset(args, 0, CMD_ARGS_LENGTH + 1);
}

static void command_parse_char(char c)
{
    /* on new line we fire commmand callback if we found the command */
    if (c == '\r') {
        uint8_t cmd_idx = 0;
        state = CMD_COMMAND;
        cmd_idx = find_command(cmd_buf);
        run_command(cmd_idx);
    } else {
        switch (state) {
        case CMD_IDLE:
            if (c != ' ')
                state = CMD_COMMAND;
            else
                break;
        case CMD_COMMAND:
            /* on first space we change over to ARGS */
            if (c != ' ') {
                *cmd = c;
                cmd++;
            } else {
                state = CMD_ARGS;
            }
            break;
        case CMD_ARGS:
            *args = c;
            args++;
            break;
        default:
            state = CMD_COMMAND;
        }
    }
}

void command_parser(char* buf, uint32_t len)
{
    /* Echo */
    while (len--) {
        putchar(*buf);
        command_parse_char(*buf);
        buf++;
    }
}

/*
 * Add command to list
 */
error_code_t command_register(command_t* cmd)
{
    if (max_cmd < CMD_MAX) {
        cmd_list[max_cmd++] = cmd;
        return PM_OK;
    }
    return OUT_OF_BOUNDS;
}

/*
 * Initialize command parser buffer and state machine
 */
error_code_t command_init()
{
    cmd_buf = RTOS_Malloc(sizeof(char) * (CMD_LENGTH + 1));
    cmd = cmd_buf;
    args_buf = RTOS_Malloc(sizeof(char) * (CMD_ARGS_LENGTH + 1));
    args = args_buf;
    state = CMD_COMMAND;
    command_register(&unknown_cmd);
    return PM_OK;
}
