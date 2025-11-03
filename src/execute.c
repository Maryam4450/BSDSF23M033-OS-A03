#include "shell.h"

/* Compatibility helper: execute a single argv (foreground) using pipeline executor */
void execute_command(char **args) {
    if (!args) return;
    cmd_t single;
    single.argv = args;
    single.infile = NULL;
    single.outfile = NULL;
    /* use execute_pipeline for single command, foreground */
    execute_pipeline(&single, 1, 0, NULL);
}










