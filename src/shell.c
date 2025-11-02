
#include <readline/readline.h>
#include <readline/history.h>
#include "shell.h"



static char* history[HISTORY_SIZE];
static int history_count = 0;

// ---------------- TOKENIZER -----------------
char** tokenize(char* cmdline) {
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n')
        return NULL;

    char** arglist = malloc(sizeof(char*) * (MAXARGS + 1));
    for (int i = 0; i < MAXARGS + 1; i++) {
        arglist[i] = malloc(ARGLEN);
        bzero(arglist[i], ARGLEN);
    }

    char* cp = cmdline;
    char* start;
    int len;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++;
        if (*cp == '\0') break;

        start = cp;
        len = 1;
        while (*++cp != '\0' && !(*cp == ' ' || *cp == '\t')) len++;
        strncpy(arglist[argnum], start, len);
        arglist[argnum][len] = '\0';
        argnum++;
    }

    if (argnum == 0) {
        for (int i = 0; i < MAXARGS + 1; i++) free(arglist[i]);
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
}

// ---------------- HISTORY -----------------
void add_to_history(const char* cmd) {
    if (cmd == NULL || strlen(cmd) == 0)
        return;

    if (history_count < HISTORY_SIZE) {
        history[history_count++] = strdup(cmd);
    } else {
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++)
            history[i - 1] = history[i];
        history[HISTORY_SIZE - 1] = strdup(cmd);
    }
    add_history(cmd); // readline built-in
}

void print_history() {
    for (int i = 0; i < history_count; i++)
        printf("%d %s\n", i + 1, history[i]);
}

char* get_history_command(int n) {
    if (n <= 0 || n > history_count) {
        printf("No such command in history.\n");
        return NULL;
    }
    return strdup(history[n - 1]);
}

// ---------------- BUILT-INS -----------------
int handle_builtin(char** arglist) {
    if (strcmp(arglist[0], "exit") == 0) {
        printf("Exiting myshell...\n");
        exit(0);
    }
    else if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else if (chdir(arglist[1]) != 0) {
            perror("cd failed");
        }
        return 1;
    }
    else if (strcmp(arglist[0], "help") == 0) {
        printf("myshell built-in commands:\n");
        printf("  cd <dir>     - change directory\n");
        printf("  exit         - exit shell\n");
        printf("  help         - show this help message\n");
        printf("  jobs         - placeholder for job control\n");
        printf("  history      - show command history\n");
        printf("  !n           - execute nth command from history\n");
        return 1;
    }
    else if (strcmp(arglist[0], "jobs") == 0) {
        printf("Job control not yet implemented.\n");
        return 1;
    }
    else if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    return 0; // Not a built-in
}


