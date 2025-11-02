#include "shell.h"

int execute(char* arglist[]) {
    int status;
    int cpid = fork();

    switch (cpid) {
        case -1:
            perror("fork failed");
            exit(1);

        case 0: // Child process
            execvp(arglist[0], arglist);
            perror("Command not found"); // Only runs if execvp fails
            exit(1);

        default: // Parent process
            waitpid(cpid, &status, 0);
            return 0;
    }
}

