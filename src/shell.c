#define _GNU_SOURCE
#include "shell.h"

/* ------------------------ History ------------------------ */
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
    add_history(s); /* readline internal */
}

void print_history(void) {
    for (int i = 0; i < history_count; ++i) {
        printf("%4d  %s\n", i+1, history_buf[i]);
    }
}

char *get_history_command(int n) {
    if (n <= 0 || n > history_count) return NULL;
    return strdup(history_buf[n-1]);
}

/* ------------------------ Jobs ------------------------ */
static job_t jobs[JOBS_MAX];
static int jobs_count = 0;

void add_job(pid_t pid, const char *cmdline) {
    if (jobs_count >= JOBS_MAX) {
        fprintf(stderr, "jobs list full, cannot add background job\n");
        return;
    }
    jobs[jobs_count].pid = pid;
    jobs[jobs_count].cmdline = strdup(cmdline ? cmdline : "(background)");
    jobs_count++;
    printf("[bg] started pid %d: %s\n", pid, cmdline ? cmdline : "(background)");
}

void remove_job(pid_t pid) {
    for (int i = 0; i < jobs_count; ++i) {
        if (jobs[i].pid == pid) {
            free(jobs[i].cmdline);
            for (int j = i + 1; j < jobs_count; ++j) jobs[j-1] = jobs[j];
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

void reap_finished_jobs(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < jobs_count; ++i) {
            if (jobs[i].pid == pid) {
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
    }
}

/* ------------------------ Utilities ------------------------ */
void free_argv(char **argv) {
    if (!argv) return;
    for (int i = 0; argv[i]; ++i) free(argv[i]);
    free(argv);
}

/* Tokenize by whitespace (returns NULL-terminated array). Caller frees with free_argv(). */
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

/* ------------------------ Parsing pipeline & redirection ------------------------ */
/* parse_pipeline(): split on '|' into stages; each stage tokenized and detects < and >.
   Allocates cmd_t array (caller must free via free_pipeline). */
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

    if (stages_n == 0) { free(stages); return -1; }

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
        if (!toks) { free_pipeline(cmds, stages_n); free(stages); return -1; }

        char **argv = malloc(sizeof(char*) * (tokcount + 1));
        int argc = 0;
        for (int j = 0; toks[j]; ++j) {
            if (strcmp(toks[j], "<") == 0) {
                if (!toks[j+1]) { free_argv(toks); free_pipeline(cmds, stages_n); free(stages); return -1; }
                cmds[i].infile = strdup(toks[j+1]);
                ++j;
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

/* ------------------------ Built-ins ------------------------ */
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

/* ------------------------ Execute pipeline ------------------------ */
/* execute_pipeline: n stages. If background==1, parent does not wait and job is recorded.
   cmdline_copy is a printable copy used for job description when background. */
int execute_pipeline(cmd_t *cmds, int n, int background, char *cmdline_copy) {
    if (!cmds || n <= 0) return -1;

    /* if single-stage and not background and builtin, run in shell */
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
            /* child */
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
            /* parent */
            pids[i] = pid;
            if (i > 0) close(pipes[i-1][0]);
            if (i < n-1) close(pipes[i][1]);
        }
    }

    if (n > 1) {
        for (int j = 0; j < n-1; ++j) {
            free(pipes[j]);
        }
        free(pipes);
    }

    if (background) {
        add_job(pids[n-1], cmdline_copy ? cmdline_copy : "(background)");
        free(pids);
        return 0;
    } else {
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

/* ------------------------ if-then-else handling ------------------------ */
/* read_if_block: reads lines from readline until matching 'fi'. Expects 'then' / 'else' keywords. */
static int read_if_block(char ***then_lines, int *then_count, char ***else_lines, int *else_count) {
    *then_lines = NULL;
    *else_lines = NULL;
    *then_count = *else_count = 0;
    int then_cap = 8, else_cap = 8;
    *then_lines = malloc(sizeof(char*) * then_cap);
    *else_lines = malloc(sizeof(char*) * else_cap);

    char *line = NULL;
    char mode = 'N'; /* 'N' none yet, 'T' then, 'E' else */

    while (1) {
        line = readline("> ");
        if (!line) {
            printf("\nEOF inside if-block\n");
            break;
        }
        /* trim leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') { free(line); continue; }

        if (strcmp(p, "then") == 0) {
            mode = 'T';
        } else if (strcmp(p, "else") == 0) {
            mode = 'E';
        } else if (strcmp(p, "fi") == 0) {
            free(line);
            break;
        } else {
            if (mode == 'T') {
                if (*then_count >= then_cap) { then_cap *= 2; *then_lines = realloc(*then_lines, sizeof(char*) * then_cap); }
                (*then_lines)[(*then_count)++] = strdup(p);
            } else if (mode == 'E') {
                if (*else_count >= else_cap) { else_cap *= 2; *else_lines = realloc(*else_lines, sizeof(char*) * else_cap); }
                (*else_lines)[(*else_count)++] = strdup(p);
            } else {
                /* lines before then are ignored (or could be error) */
            }
        }
        free(line);
    }
    return 0;
}

/* execute_lines: run array of lines (each may be full pipeline/chaining). background flag passed to execute_pipeline calls. */
static void execute_lines(char **lines, int n, int background) {
    for (int i = 0; i < n; ++i) {
        cmd_t *cmds = NULL;
        int ncmds = 0;
        if (parse_pipeline(lines[i], &cmds, &ncmds) == 0) {
            char *copy = strdup(lines[i]);
            execute_pipeline(cmds, ncmds, background, copy);
            if (!background) free(copy);
            free_pipeline(cmds, ncmds);
        } else {
            fprintf(stderr, "Parse error in line: %s\n", lines[i]);
        }
    }
}

/* handle_if_then_else: receives the text following 'if ' (condition). It executes the condition, reads blocks, and runs chosen block. */
static int handle_if_then_else(const char *cond_text) {
    if (!cond_text) return -1;
    /* execute condition */
    cmd_t *cond_cmds = NULL;
    int ncond = 0;
    if (parse_pipeline(cond_text, &cond_cmds, &ncond) != 0) {
        fprintf(stderr, "Parse error in if condition: %s\n", cond_text);
        return -1;
    }
    int cond_status = execute_pipeline(cond_cmds, ncond, 0, NULL);
    free_pipeline(cond_cmds, ncond);

    /* read then / else block */
    char **then_lines = NULL, **else_lines = NULL;
    int then_count = 0, else_count = 0;
    read_if_block(&then_lines, &then_count, &else_lines, &else_count);

    if (cond_status == 0) {
        execute_lines(then_lines, then_count, 0);
    } else {
        execute_lines(else_lines, else_count, 0);
    }

    for (int i = 0; i < then_count; ++i) free(then_lines[i]);
    for (int i = 0; i < else_count; ++i) free(else_lines[i]);
    free(then_lines);
    free(else_lines);
    return 0;
}

/* ------------------------ Main shell loop ------------------------ */
void start_shell(void) {
    char *line = NULL;

    while (1) {
        /* Reap finished background jobs */
        reap_finished_jobs();

        line = readline(PROMPT);
        if (!line) {
            printf("\n");
            break;
        }

        /* trim leading whitespace */
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') { free(line); continue; }

        /* store in history */
        add_to_our_history(p);

        /* if starts with 'if ' handle control structure */
        if (strncmp(p, "if ", 3) == 0) {
            handle_if_then_else(p + 3);
            free(line);
            continue;
        }

        /* split on ';' for chaining */
        char *saveptr = NULL;
        char *segment = strtok_r(p, ";", &saveptr);
        while (segment) {
            /* trim segment */
            while (*segment == ' ' || *segment == '\t') ++segment;
            char *end = segment + strlen(segment) - 1;
            while (end > segment && (*end == ' ' || *end == '\t')) { *end = '\0'; --end; }
            if (*segment == '\0') { segment = strtok_r(NULL, ";", &saveptr); continue; }

            /* detect trailing '&' for background */
            int background = 0;
            size_t seglen = strlen(segment);
            if (seglen > 0 && segment[seglen-1] == '&') {
                background = 1;
                /* remove trailing & and trailing whitespace */
                char *q = segment + seglen - 1;
                *q = '\0';
                while (q > segment && (*(q-1) == ' ' || *(q-1) == '\t')) { --q; *(q) = '\0'; }
            }

            /* handle !n substitution before parsing */
            if (segment[0] == '!') {
                long n = strtol(segment+1, NULL, 10);
                char *found = get_history_command((int)n);
                if (found) {
                    printf("%s\n", found);
                    /* replace segment content pointer with found (heap allocated) */
                    char *tmp = strdup(found);
                    free(found);
                    /* use tmp as the segment content for parse_pipeline */
                    cmd_t *cmds = NULL;
                    int ncmds = 0;
                    if (parse_pipeline(tmp, &cmds, &ncmds) == 0) {
                        char *copy = strdup(tmp);
                        execute_pipeline(cmds, ncmds, background, copy);
                        if (!background) free(copy);
                        free_pipeline(cmds, ncmds);
                    } else {
                        fprintf(stderr, "Parse error in history expansion: %s\n", tmp);
                    }
                    free(tmp);
                    segment = strtok_r(NULL, ";", &saveptr);
                    continue;
                } else {
                    fprintf(stderr, "No such command in history: %ld\n", n);
                    segment = strtok_r(NULL, ";", &saveptr);
                    continue;
                }
            }

            /* Normal parse & execute for this segment */
            cmd_t *cmds = NULL;
            int ncmds = 0;
            if (parse_pipeline(segment, &cmds, &ncmds) == 0) {
                char *copy = strdup(segment);
                execute_pipeline(cmds, ncmds, background, copy);
                if (!background) free(copy);
                free_pipeline(cmds, ncmds);
            } else {
                fprintf(stderr, "Parse error: %s\n", segment);
            }

            segment = strtok_r(NULL, ";", &saveptr);
        }

        free(line);
    }

    /* cleanup: reap and free history/jobs */
    reap_finished_jobs();
    for (int i = 0; i < history_count; ++i) free(history_buf[i]);
    for (int i = 0; i < jobs_count; ++i) free(jobs[i].cmdline);
}


