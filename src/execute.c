#include "shell.h"

/* Compatibility helper: execute a single argv (foreground) */
void execute(char **arglist) {
    cmd_t single;
    single.argv = arglist;
    single.infile = NULL;
    single.outfile = NULL;
    execute_pipeline(&single, 1, 0, NULL);
}




