#define _GNU_SOURCE
#include "shell.h"

/* ---------- History (simple array mirrored with readline) ---------- */
static char *history_buf[HISTORY_SIZE];
static int history_count = 0;

void add_to_our_history(const char *s) {
    if (!s || *s == '\0') return;
    if (history_count < HISTORY_SIZE) {
        history_buf[history_count++] = strdup(s);
    } else {
        free(history_buf[0]);
        for (int i = 1; i < HISTORY_SIZE; ++i) history_buf[i-1] = history_buf[i];
        history_buf[HISTORY_SIZE-1] = strdup(s);
    }
    add_history(s); /* readline history */
}

void print_history() {
    for (int i = 0; i < history_count; ++i) {
        printf("%3d  %s\n", i+1, history_buf[i]);
    }
}

char *get_history_command(int n) {
    if (n <= 0 || n > history_count) return NULL;
    return strdup(history_buf[n-1]);
}

/* ---------- Utility: free argv ---------- */
void free_argv(char **argv) {
    if (!argv) return;
    for (int i = 0; argv[i]; ++i) free(argv[i]);
    free(argv);
}

/* ---------- Tokenize by whitespace (returns NULL-terminated vector) ----------
   Caller must free with free_argv(). */
char **tokenize_whitespace(const char *s, int *out_count) {
    if (!s) { if (out_count) *out_count = 0; return NULL; }
    // Make a mutable copy
    char *buf = strdup(s);
    if (!buf) return NULL;

    int capacity = 16;
    char **arr = malloc(sizeof(char*) * capacity);
    int count = 0;

    char *p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        char *start = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        int len = p - start;
        char *tok = malloc(len + 1);
        memcpy(tok, start, len);
        tok[len] = '\0';
        if (count + 2 >= capacity) {
            capacity *= 2;
            arr = realloc(arr, sizeof(char*) * capacity);
        }
        arr[count++] = tok;
    }
    arr[count] = NULL;
    free(buf);
    if (out_count) *out_count = count;
    return arr;
}

/* ---------- PARSING: parse a pipeline line into commands ----------
   Supports multiple '|' separated stages.
   Each stage supports optional '< infile' and/or '> outfile'.
   Returns 0 on success, and allocates cmds (caller must free_pipeline).
*/
int parse_pipeline(const char *line, cmd_t **out_cmds, int *out_n) {
    if (!line) return -1;

    // Split on '|' first (preserve order). We will handle trimming.
    // Make a copy to use strtok on.
    char *buf = strdup(line);
    if (!buf) return -1;

    // Count stages
    int stages_cap = 8;
    char **stages = malloc(sizeof(char*) * stages_cap);
    int stages_n = 0;

    char *saveptr = NULL;
    char *token = strtok_r(buf, "|", &saveptr);
    while (token) {
        // trim leading/trailing spaces
        while (*token == ' ' || *token == '\t') ++token;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) { *end = '\0'; --end; }

        if (stages_n + 1 >= stages_cap) {
            stages_cap *= 2;
            stages = realloc(stages, sizeof(char*) * stages_cap);
        }
        stages[stages_n++] = strdup(token);
        token = strtok_r(NULL, "|", &saveptr);
    }
    free(buf);

    if (stages_n == 0) {
        free(stages);
        return -1;
    }

    // Allocate cmd_t array
    cmd_t *cmds = calloc(stages_n, sizeof(cmd_t));
    for (int i = 0; i < stages_n; ++i) {
        cmds[i].argv = NULL;
        cmds[i].infile = NULL;
        cmds[i].outfile = NULL;
    }

    // For each stage, tokenize and detect < and >
    for (int i = 0; i < stages_n; ++i) {
        int tokcount = 0;
        char **toks = tokenize_whitespace(stages[i], &tokcount);
        free(stages[i]);
        if (!toks) {
            // empty stage -> error
            free_pipeline(cmds, stages_n);
            free(stages);
            return -1;
        }

        // Build argv by skipping redirection tokens and capturing filenames
        char **argv = malloc(sizeof(char*) * (tokcount + 1)); // safe upper bound
        int argc = 0;
        for (int j = 0; toks[j]; ++j) {
            if (strcmp(toks[j], "<") == 0) {
                if (!toks[j+1]) { /* syntax error */ free_argv(toks); free_pipeline(cmds, stages_n); free(stages); return -1; }
                cmds[i].infile = strdup(toks[j+1]);
                ++j; // skip filename
            } else if (strcmp(toks[j], ">") == 0) {
                if (!toks[j+1]) { free_argv(toks); free_pipeline(cmds, stages_n); free(stages); return -1; }
                cmds[i].outfile = strdup(toks[j+1]);
                ++j;
            } else {
                argv[argc++] = strdup(toks[j]);
            }
        }
        argv[argc] = NULL;
        cmds[i].argv = argv;
        free_argv(toks);
    }

    free(stages);
    *out_cmds = cmds;
    *out_n = stages_n;
    return 0;
}

void free_pipeline(cmd_t *cmds, int n) {
    if (!cmds) return;
    for (int i = 0; i < n; ++i) {
        if (cmds[i].argv) free_argv(cmds[i].argv);
        if (cmds[i].infile) free(cmds[i].infile);
        if (cmds[i].outfile) free(cmds[i].outfile);
    }
    free(cmds);
}

/* ---------- Built-ins ---------- */
int handle_builtin(char **argv) {
    if (!argv || !argv[0]) return 0;
    if (strcmp(argv[0], "exit") == 0) {
        printf("Exiting myshell...\n");
        exit(0);
    } else if (strcmp(argv[0], "cd") == 0) {
        if (!argv[1]) {
            fprintf(stderr, "cd: missing argument\n");
        } else {
            if (chdir(argv[1]) != 0) perror("cd");
        }
        return 1;
    } else if (strcmp(argv[0], "help") == 0) {
        printf("Built-ins:\n  cd <dir>\n  exit\n  help\n  jobs\n  history\n  !n\n");
        return 1;
    } else if (strcmp(argv[0], "jobs") == 0) {
        printf("Job control not yet implemented.\n");
        return 1;
    } else if (strcmp(argv[0], "history") == 0) {
        print_history();
        return 1;
    }
    return 0;
}

/* ---------- Execute a pipeline of n commands ----------
   Uses pipes for >1 stage; supports per-stage infile/outfile.
   Returns exit status of last command (best-effort).
*/
int execute_pipeline(cmd_t *cmds, int n) {
    if (!cmds || n <= 0) return -1;

    /* If single stage and builtin -> execute builtin in shell process */
    if (n == 1 && handle_builtin(cmds[0].argv)) {
        return 0;
    }

    int **pipes = NULL; // array of pipe fds
    if (n > 1) {
        pipes = malloc(sizeof(int*) * (n-1));
        for (int i = 0; i < n-1; ++i) {
            pipes[i] = malloc(sizeof(int) * 2);
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
                // free
                for (int k = 0; k <= i; ++k) if (pipes[k]) free(pipes[k]);
                free(pipes);
                return -1;
            }
        }
    }

    pid_t *pids = malloc(sizeof(pid_t) * n);

    for (int i = 0; i < n; ++i) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            // cleanup omitted for brevity
            continue;
        } else if (pid == 0) {
            /* Child process */
            // If not first stage, replace stdin with read end of previous pipe
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            // If not last stage, replace stdout with write end of current pipe
            if (i < n-1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // Handle infile/outfile for this stage (overrides pipe behavior if used)
            if (cmds[i].infile) {
                int fd = open(cmds[i].infile, O_RDONLY);
                if (fd < 0) {
                    perror("open infile");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            if (cmds[i].outfile) {
                int fd = open(cmds[i].outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("open outfile");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            /* Close all pipe fds in child */
            if (n > 1) {
                for (int j = 0; j < n-1; ++j) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
            }

            /* execvp the command */
            if (cmds[i].argv && cmds[i].argv[0]) {
                execvp(cmds[i].argv[0], cmds[i].argv);
            }
            perror("execvp");
            exit(1);
        } else {
            /* Parent */
            pids[i] = pid;
            // parent closes fds it doesn't need immediately
            if (i > 0) {
                // close read end of previous pipe in parent
                close(pipes[i-1][0]);
            }
            if (i < n-1) {
                // close write end of current pipe in parent after next child will use it
                close(pipes[i][1]);
            }
        }
    }

    /* Parent: close any remaining pipe fds */
    if (n > 1) {
        for (int j = 0; j < n-1; ++j) {
            // these were already closed as we looped, but ensure closed
            // (safe to ignore errors)
            //close(pipes[j][0]); close(pipes[j][1]);
            free(pipes[j]);
        }
        free(pipes);
    }

    /* Wait for all children and collect status of last */
    int last_status = 0;
    for (int i = 0; i < n; ++i) {
        int status = 0;
        waitpid(pids[i], &status, 0);
        if (i == n-1) last_status = status;
    }
    free(pids);
    return WEXITSTATUS(last_status);
}

/* ---------- Main shell loop (readline + parsing + execution) ---------- */
void start_shell(void) {
    char *line = NULL;

    while (1) {
        line = readline(PROMPT);
        if (!line) {
            printf("\n");
            break; // Ctrl+D
        }

        // trim leading whitespace
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') { free(line); continue; }

        /* Handle !n before storing */
        if (*p == '!') {
            long n = strtol(p+1, NULL, 10);
            char *found = get_history_command((int)n);
            if (found) {
                printf("%s\n", found);
                free(line);
                line = found;
            } else {
                fprintf(stderr, "No such command in history: %ld\n", n);
                free(line);
                continue;
            }
        }

        add_to_our_history(line);

        /* Parse pipeline */
        cmd_t *cmds = NULL;
        int ncmds = 0;
        if (parse_pipeline(line, &cmds, &ncmds) == 0) {
            execute_pipeline(cmds, ncmds);
            free_pipeline(cmds, ncmds);
        } else {
            fprintf(stderr, "Parse error\n");
        }

        free(line);
    }

    /* free history buffer */
    for (int i = 0; i < history_count; ++i) free(history_buf[i]);
    history_count = 0;
}



