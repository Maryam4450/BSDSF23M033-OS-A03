#include "shell.h"

/* main: initialize readline history and start shell loop */
int main() {
    /* initialize readline history support */
    using_history();
    /* enable tab completion (default readline handler) */
    rl_bind_key('\t', rl_complete);

    start_shell();
    return 0;
}

