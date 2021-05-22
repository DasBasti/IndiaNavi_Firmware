/*
 * command.h
 *
 *  Created on: Jan 16, 2021
 *      Author: bastian
 */

#ifndef PLATINENMACHER_PARSER_COMMAND_H_
#define PLATINENMACHER_PARSER_COMMAND_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "memory.h"

#ifndef CMD_MAX
#define CMD_MAX 10
#endif
#ifndef CMD_LENGTH
#define CMD_LENGTH 10
#endif
#ifndef CMD_ARGS_LENGTH
#define CMD_ARGS_LENGTH 70
#endif

typedef enum
{
	CMD_IDLE,
	CMD_COMMAND,
	CMD_ARGS
} cmd_t;

typedef struct command
{
	char *command;
	char *args;

	void (*function_cb)(struct command *cmd);
} command_t;

void command_parser(char *buf, uint32_t len);
error_code_t command_init();
error_code_t command_register(command_t *cmd);

#endif /* PLATINENMACHER_PARSER_COMMAND_H_ */
