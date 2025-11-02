#include "shell.h"

/* History storage */
static char *history[HISTORY_SIZE];
static int history_count = 0; // total commands stored (<= HISTORY_SIZE)
static int history_next = 0;  // next slot to write (for circular behavior)

/* Initialize history storage (call once at startup) */
void init_history(void) {
    for (int i = 0; i < HISTORY_SIZE; i++) {
        history[i] = NULL;
    }
    history_count = 0;
    history_next = 0;
}

/* Add a command line to history (makes its own copy) */
void add_history(const char *cmdline) {
    if (cmdline == NULL) return;

    // If there's an existing string in the slot, free it
    if (history[history_next] != NULL) {
        free(history[history_next]);
        history[history_next] = NULL;
    }

    history[history_next] = strdup(cmdline);
    if (history[history_next] == NULL) {
        perror("strdup");
        return;
    }

    history_next = (history_next + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) history_count++;
}

/* Print history with 1-based numbering (oldest first) */
void print_history(void) {
    int start;
    if (history_count < HISTORY_SIZE) {
        start = 0;
    } else {
        start = history_next; // oldest entry
    }

    for (int i = 0; i < history_count; i++) {
        int idx = (start + i) % HISTORY_SIZE;
        printf("%2d  %s\n", i + 1, history[idx]);
    }
}

/* Retrieve the n-th command (1-based) in chronological order */
const char* get_history(int n) {
    if (n <= 0 || n > history_count) {
        return NULL;
    }

    int start;
    if (history_count < HISTORY_SIZE) {
        start = 0;
    } else {
        start = history_next;
    }
    int idx = (start + (n - 1)) % HISTORY_SIZE;
    return history[idx];
}

/* Free all allocated history strings */
void free_history(void) {
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (history[i] != NULL) {
            free(history[i]);
            history[i] = NULL;
        }
    }
    history_count = 0;
    history_next = 0;
}

/* ---------- Input reading and tokenization ---------- */

char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    fflush(stdout);

    char* cmdline = (char*) malloc(sizeof(char) * MAX_LEN);
    if (!cmdline) {
        perror("malloc");
        return NULL;
    }

    int c, pos = 0;
    while ((c = getc(fp)) != EOF) {
        if (c == '\n') break;
        if (pos < MAX_LEN - 1) {
            cmdline[pos++] = c;
        }
    }

    if (c == EOF && pos == 0) {
        free(cmdline);
        return NULL; // Handle Ctrl+D
    }

    cmdline[pos] = '\0';
    return cmdline;
}

char** tokenize(char* cmdline) {
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    if (!arglist) {
        perror("malloc");
        return NULL;
    }

    for (int i = 0; i < MAXARGS + 1; i++) {
        arglist[i] = (char*)malloc(sizeof(char) * ARGLEN);
        if (!arglist[i]) {
            perror("malloc");
            // free previously allocated and return
            for (int j = 0; j < i; j++) free(arglist[j]);
            free(arglist);
            return NULL;
        }
        bzero(arglist[i], ARGLEN);
    }

    char* cp = cmdline;
    char* start;
    int len;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++; // Skip whitespace

        if (*cp == '\0') break;

        start = cp;
        len = 1;
        while (*++cp != '\0' && !(*cp == ' ' || *cp == '\t')) {
            len++;
        }
        // copy token
        if (len >= ARGLEN) len = ARGLEN - 1;
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

/* Built-in handler (history builtin added here) */
int handle_builtin(char **arglist) {
    if (arglist == NULL || arglist[0] == NULL)
        return 0;

    // exit command
    if (strcmp(arglist[0], "exit") == 0) {
        printf("Exiting shell...\n");
        free_history();
        exit(0);
    }

    // cd command
    if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else if (chdir(arglist[1]) != 0) {
            perror("cd failed");
        }
        return 1;
    }

    // help command
    if (strcmp(arglist[0], "help") == 0) {
        printf("Built-in commands:\n");
        printf("  cd <dir>   - Change directory\n");
        printf("  exit       - Exit the shell\n");
        printf("  help       - Show this help message\n");
        printf("  jobs       - Show background jobs (not implemented yet)\n");
        printf("  history    - Show command history\n");
        printf("  !n         - Re-execute nth command from history (1-based)\n");
        return 1;
    }

    // jobs command
    if (strcmp(arglist[0], "jobs") == 0) {
        printf("Job control not yet implemented.\n");
        return 1;
    }

    // history command
    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    return 0; // not a built-in
}

