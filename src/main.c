#include "shell.h"

int main() {
    char* cmdline;
    char** arglist;

    while (1) {
        cmdline = readline(PROMPT);

        if (cmdline == NULL) { // Ctrl+D
            printf("\nExiting myshell...\n");
            break;
        }

        if (cmdline[0] == '\0') {
            free(cmdline);
            continue;
        }

        // Check for !n re-execution before storing
        if (cmdline[0] == '!') {
            int n = atoi(cmdline + 1);
            free(cmdline);
            cmdline = get_history_command(n);
            if (cmdline == NULL)
                continue;
            printf("%s\n", cmdline);
        }

        add_to_history(cmdline);

        arglist = tokenize(cmdline);
        if (arglist == NULL) {
            free(cmdline);
            continue;
        }

        if (!handle_builtin(arglist))
            execute(arglist);

        for (int i = 0; arglist[i] != NULL; i++)
            free(arglist[i]);
        free(arglist);
        free(cmdline);
    }

    return 0;
}


