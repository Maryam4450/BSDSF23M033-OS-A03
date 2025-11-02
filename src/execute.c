/* execute.c can remain minimal because execution logic is in shell.c's execute_pipeline.
   Provide a stub for compatibility if other code called execute_command previously. */

#include "shell.h"

/* If some code calls execute_command(argc), keep compatibility */
void execute_command(char **args) {
    cmd_t single;
    single.argv = args;
    single.infile = NULL;
    single.outfile = NULL;
    execute_pipeline(&single, 1);
}



