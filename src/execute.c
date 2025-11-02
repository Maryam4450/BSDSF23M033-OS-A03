#include "shell.h"

int execute(char* arglist[]) {
    int status;
    pid_t cpid = fork();

    if (cpid < 0) {
        perror("fork failed");
        return -1;
    }

    if (cpid == 0) { // Child
        execvp(arglist[0], arglist);
        perror("Command not found"); // execvp only returns on failure
        exit(1);
    } else { // Parent
        waitpid(cpid, &status, 0);
        return 0;
    }
}




