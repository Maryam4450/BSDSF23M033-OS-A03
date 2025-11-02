#include "shell.h"

/* main simply starts the shell */
int main() {
    /* Initialize readline history */
    using_history();
    rl_bind_key('\t', rl_complete); /* enable tab completion (default) */

    start_shell();
    return 0;
}

