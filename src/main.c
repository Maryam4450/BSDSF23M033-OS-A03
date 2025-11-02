#include "shell.h"

int main() {
    init_history();

    char* cmdline;
    char** arglist;

    while ((cmdline = read_cmd(PROMPT, stdin)) != NULL) {

        /* Trim leading whitespace quickly so we can check first char reliably */
        char *p = cmdline;
        while (*p == ' ' || *p == '\t') p++;

        /* Handle !n re-execution BEFORE tokenization or adding to history */
        if (*p == '!') {
            char *numstr = p + 1;
            // skip leading spaces after '!' (optional)
            while (*numstr == ' ' || *numstr == '\t') numstr++;

            if (*numstr == '\0') {
                fprintf(stderr, "Usage: !n (n is history index)\n");
                free(cmdline);
                continue;
            }

            char *endptr;
            long n = strtol(numstr, &endptr, 10);
            if (endptr == numstr || n <= 0) {
                fprintf(stderr, "Invalid history reference: %s\n", numstr);
                free(cmdline);
                continue;
            }

            const char *found = get_history((int)n);
            if (found == NULL) {
                fprintf(stderr, "No such command in history: %ld\n", n);
                free(cmdline);
                continue;
            }

            /* Replace cmdline with a copy of the found command */
            free(cmdline);
            cmdline = strdup(found);
            if (cmdline == NULL) {
                perror("strdup");
                continue;
            }
            printf("%s\n", cmdline); // echo the command being executed
        }

        /* If after trimming we have empty command, skip */
        if (cmdline == NULL) continue;
        char *tmp = cmdline;
        while (*tmp == ' ' || *tmp == '\t' || *tmp == '\n') tmp++;
        if (*tmp == '\0') {
            free(cmdline);
            continue;
        }

        /* Add the (possibly expanded) command to history */
        add_history(cmdline);

        /* Tokenize and run */
        if ((arglist = tokenize(cmdline)) != NULL) {

            // Built-in handling first
            if (!handle_builtin(arglist)) {
                execute(arglist);
            }

            // Free token array memory
            for (int i = 0; arglist[i] != NULL; i++) {
                free(arglist[i]);
            }
            free(arglist);
        }

        free(cmdline);
    }

    printf("\nShell exited.\n");
    free_history();
    return 0;
}

