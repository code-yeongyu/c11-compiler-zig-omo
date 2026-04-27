#include "test_support.h"
#include "022_quick_exit.c"

#include <string.h>

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "child") == 0) {
        if (at_quick_exit(quick_exit_write_sentinel) != 0) {
            return 125;
        }
        quick_exit(0);
    }

    /* given */
    (void)remove(quick_exit_probe_path);
    const int child_status = system("./022_quick_exit_test child");

    /* when */
    const struct quick_exit_result result = quick_exit_run(child_status);

    /* then */
    assert(result.child_status != -1);
    assert(result.sentinel_written == 1);
    (void)remove(quick_exit_probe_path);
    C11_REF_OK();
}
