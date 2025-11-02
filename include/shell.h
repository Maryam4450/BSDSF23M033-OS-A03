#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAXARGS 128
#define ARGLEN 256
#define PROMPT "PUCIT> "  
#define HISTORY_SIZE 50

/* A single command in a pipeline */
typedef struct {
    char **argv;     // NULL-terminated argument vector
    char *infile;    // input redirection filename or NULL
    char *outfile;   // output redirection filename or NULL
} cmd_t;

/* Top-level functions */
void start_shell(void);
char **tokenize_whitespace(const char *s, int *count);
int handle_builtin(char **argv);
void free_argv(char **argv);

/* Pipeline parsing/execution */
int parse_pipeline(const char *line, cmd_t **out_cmds, int *out_n);
void free_pipeline(cmd_t *cmds, int n);
int execute_pipeline(cmd_t *cmds, int n);

#endif

