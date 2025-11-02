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

/* ---------- Jobs (background processes) ---------- */
static job_t jobs[JOBS_MAX];
static int jobs_count = 0;

void add_job(pid_t pid, const char *cmdline) {
    if (jobs_count >= JOBS_MAX) {
        fprintf(stderr, "jobs list full, cannot add background job\n");
        return;
    }
    jobs[jobs_count].pid = pid;
    jobs[jobs_count].cmdline = strdup(cmdline);
    jobs_count++;
    printf("[bg] started pid %d: %s\n", pid, cmdline);
}

void remove_job(pid_t pid) {
    for (int i = 0; i < jobs_count; ++i) {
        if (jobs[i].pid == pid) {
            free(jobs[i].cmdline);
            for (int j = i+1; j < jobs_count; ++j) jobs[j-1] = jobs[j];
            jobs_count--;
            return;
        }
    }
}

void list_jobs(void) {
    for (int i = 0; i < jobs_count; ++i) {
        printf("[%d] pid:%d  %s\n", i+1, jobs[i].pid, jobs[i].cmdline);
    }
}

/* Reap any finished child processes (non-blocking) and remove from jobs list */
void reap_finished_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // if pid was a background job, remove it and print exit info
        int found = 0;
        for (int i = 0; i < jobs_count; ++i) {
            if (jobs[i].pid == pid) {
                found = 1;
                if (WIFEXITED(status)) {
                    printf("\n[bg] pid %d finished (exit %d): %s\n", pid, WEXITSTATUS(status), jobs[i].cmdline);
                } else if (WIFSIGNALED(status)) {
                    printf("\n[bg] pid %d terminated by signal %d: %s\n", pid, WTERMSIG(status), jobs[i].cmdline);
                } else {
                    printf("\n[bg] pid %d finished: %s\n", pid, jobs[i].cmdline);
                }
                remove_job(pid);
                break;
            }
        }
        // If it wasn't in our jobs list, it's probably a foreground child we already waited for; ignore.
    }
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

    char *buf = strdup(line);
    if (!buf) return -1;

    int stages_cap = 8;
    char **stages = malloc(sizeof(char*) * stages_cap);
    int stages_n = 0;

    char *saveptr = NULL;
    char *token = strtok_r(buf, "|", &saveptr);
    while (token) {
        // trim
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

    cmd_t *cmds = calloc(stages_n, sizeof(cmd_t));
    for (int i = 0; i < stages_n; ++i) {
        cmds[i].argv = NULL;
        cmds[i].infile = NULL;
        cmds[i].outfile = NULL;
    }

    for (int i = 0; i < stages_n; ++i) {
        int tokcount = 0;
        char **toks = tokenize_whitespace(stages[i], &tokcount);
        free(stages[i]);
        if (!toks) {
            free_pipeline(cmds, stages_n);
            free(stages);
            return -1;
        }

        char **argv = malloc(sizeof(char*) * (tokcount + 1));
        int argc = 0;
        for (int j = 0; toks[j]; ++j) {
            if (strcmp(toks[j], "<") == 0) {
                if (!toks[j+1]) { free_argv(toks); free_pipeline(cmds, stages_n); free(stages); return -1; }
                cmds[i].infile = strdup(toks[j+1]); ++j;
            } else if (strcmp(toks[j], ">") == 0) {
                if (!toks[j+1]) { free_argv(toks); free_pipeline(cmds, stages_n); free(stages); return -1; }
                cmds[i].outfile = strdup(toks[j+1]); ++j;
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
        list_jobs();
        return 1;
    } else if (strcmp(argv[0], "history") == 0) {
        print_history();
        return 1;
    }
    return 0;
}

/* ---------- Execute a pipeline of n commands ----------
   If background == 1, parent does not wait and background job(s) are recorded.
   cmdline_copy is a printable copy of the original command line (used when adding job).
*/
int execute_pipeline(cmd_t *cmds, int n, int background, char *cmdline_copy) {
    if (!cmds || n <= 0) return -1;

    /* If single stage and builtin -> execute builtin in shell process (foreground only) */
    if (n == 1 && !background && handle_builtin(cmds[0].argv)) {
        return 0;
    }

    int **pipes = NULL;
    if (n > 1) {
        pipes = malloc(sizeof(int*) * (n-1));
        for (int i = 0; i < n-1; ++i) {
            pipes[i] = malloc(sizeof(int) * 2);
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
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
            continue;
        } else if (pid == 0) {
            /* Child */
            if (i > 0) dup2(pipes[i-1][0], STDIN_FILENO);
            if (i < n-1) dup2(pipes[i][1], STDOUT_FILENO);

            if (cmds[i].infile) {
                int fd = open(cmds[i].infile, O_RDONLY);
                if (fd < 0) { perror("open infile"); exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            if (cmds[i].outfile) {
                int fd = open(cmds[i].outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror("open outfile"); exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            if (n > 1) {
                for (int j = 0; j < n-1; ++j) {
                    close(pipes[j][0]); close(pipes[j][1]);
                }
            }

            if (cmds[i].argv && cmds[i].argv[0]) execvp(cmds[i].argv[0], cmds[i].argv);
            perror("execvp");
            exit(1);
        } else {
            /* Parent */
            pids[i] = pid;
            if (i > 0) close(pipes[i-1][0]);
            if (i < n-1) close(pipes[i][1]);
        }
    }

    /* Close remaining pipe arrays */
    if (n > 1) {
        for (int j = 0; j < n-1; ++j) free(pipes[j]);
        free(pipes);
    }

    if (background) {
        /* For background pipeline, add the last child's pid as representative job.
           (Optionally could add group, but keep it simple and use last pid.) */
        add_job(pids[n-1], cmdline_copy ? cmdline_copy : "(background)");
        /* Parent does NOT wait; we still should not leak pids array memory */
        free(pids);
        return 0;
    } else {
        /* Foreground: wait for all children; return last exit status */
        int last_status = 0;
        for (int i = 0; i < n; ++i) {
            int status = 0;
            waitpid(pids[i], &status, 0);
            if (i == n-1) last_status = status;
        }
        free(pids);
        return WEXITSTATUS(last_status);
    }
}

/* ---------- Main shell loop (readline + parsing + execution) ----------
   This version supports splitting by ';' and detecting '&' for background execution.
*/
void start_shell(void) {
    char *line = NULL;

    while (1) {
        /* Reap any finished background jobs before prompting */
        reap_finished_jobs();

        line = readline(PROMPT);
        if (!line) {
            printf("\n");
            break; // Ctrl+D
        }

        /* Trim leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') { free(line); continue; }

        /* Split the input line into segments separated by ';' */
        char *saveptr = NULL;
        char *segment = NULL;
        char *line_copy_for_history = strdup(p); // store trimmed copy for history
        add_to_our_history(line_copy_for_history);
        free(line_copy_for_history);

        segment = strtok_r(p, ";", &saveptr);
        while (segment) {
            /* trim segment */
            while (*segment == ' ' || *segment == '\t') ++segment;
            char *end = segment + strlen(segment) - 1;
            while (end > segment && (*end == ' ' || *end == '\t')) { *end = '\0'; --end; }

            if (*segment == '\0') {
                segment = strtok_r(NULL, ";", &saveptr);
                continue;
            }

            /* Detect trailing '&' for background execution */
            int background = 0;
            size_t seglen = strlen(segment);
            if (seglen > 0 && segment[seglen-1] == '&') {
                background = 1;
                /* remove trailing & and any spaces before it */
                char *q = segment + seglen - 1;
                *q = '\0';
                while (q > segment && (*(q-1) == ' ' || *(q-1) == '\t')) { --q; *(q-1) = '\0'; }
            }

            /* Handle !n before parsing */
            if (segment[0] == '!') {
                long n = strtol(segment+1, NULL, 10);
                char *found = get_history_command((int)n);
                if (found) {
                    printf("%s\n", found);
                    free(segment); // not allocated by us, but we'll replace behavior by strdup later
                    segment = found; // found is heap-allocated, will be parsed then freed by free_pipeline/others
                } else {
                    fprintf(stderr, "No such command in history: %ld\n", n);
                    segment = strtok_r(NULL, ";", &saveptr);
                    continue;
                }
            }

            /* parse pipeline & redirection for this segment */
            cmd_t *cmds = NULL;
            int ncmds = 0;
            if (parse_pipeline(segment, &cmds, &ncmds) == 0) {
                /* keep a copy of printable segment for job list if background */
                char *cmdline_copy = strdup(segment);
                execute_pipeline(cmds, ncmds, background, cmdline_copy);
                /* for background, execute_pipeline took cmdline_copy into jobs; otherwise free it */
                if (!background) free(cmdline_copy);
                free_pipeline(cmds, ncmds);
            } else {
                fprintf(stderr, "Parse error in segment: %s\n", segment);
            }

            /* If we substituted history (!n) and got a strdup, free it */
            if (segment && segment[0] != '\0' && segment != NULL) {
                /* No reliable way to tell if segment was strdup-ed (except when we set it above)
                   so we only free those we allocated explicitly (cmdline_copy and parse frees). */
            }

            segment = strtok_r(NULL, ";", &saveptr);
        }

        free(line);
    }

    /* cleanup jobs and history */
    reap_finished_jobs();
    for (int i = 0; i < history_count; ++i) free(history_buf[i]);
    for (int i = 0; i < jobs_count; ++i) free(jobs[i].cmdline);
}

