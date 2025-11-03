#ifndef SHELL_H
#define SHELL_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <errno.h>

#define MAXARGS 128
#define ARGLEN 256
#define PROMPT "PUCIT> "
#define HISTORY_SIZE 100
#define JOBS_MAX 128

/* Representation of a single pipeline stage / command */
typedef struct {
    char **argv;     /* NULL-terminated */
    char *infile;    /* input redirection filename or NULL */
    char *outfile;   /* output redirection filename or NULL */
} cmd_t;

/* Job structure for background processes */
typedef struct {
    pid_t pid;
    char *cmdline;
} job_t;

/* Top-level shell control */
void start_shell(void);

/* Parser/execution helpers */
int parse_pipeline(const char *line, cmd_t **out_cmds, int *out_n);
void free_pipeline(cmd_t *cmds, int n);
int execute_pipeline(cmd_t *cmds, int n, int background, char *cmdline_copy);

/* Token utilities */
char **tokenize_whitespace(const char *s, int *count);
void free_argv(char **argv);

/* Built-ins & history */
int handle_builtin(char **argv);
void add_to_our_history(const char *s);
void print_history(void);
char *get_history_command(int n);

/* Job management */
void add_job(pid_t pid, const char *cmdline);
void remove_job(pid_t pid);
void list_jobs(void);
void reap_finished_jobs(void);

/* Compatibility wrapper */
void execute_command(char **args); /* convenience wrapper to execute single argv */

#endif



