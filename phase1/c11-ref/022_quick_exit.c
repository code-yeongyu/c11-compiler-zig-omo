#include <stdio.h>
#include <stdlib.h>

struct quick_exit_result {
    int child_status;
    int sentinel_written;
};

static const char *quick_exit_probe_path = ".c11_ref_quick_exit_probe.tmp";

void quick_exit_write_sentinel(void)
{
    FILE *file = fopen(quick_exit_probe_path, "w");
    if (file != NULL) {
        (void)fputc('Q', file);
        (void)fclose(file);
    }
}

int quick_exit_sentinel_exists(void)
{
    FILE *file = fopen(quick_exit_probe_path, "r");
    int found;
    if (file == NULL) {
        return 0;
    }
    found = fgetc(file) == 'Q';
    (void)fclose(file);
    return found;
}

struct quick_exit_result quick_exit_run(int child_status)
{
    return (struct quick_exit_result){
        .child_status = child_status,
        .sentinel_written = quick_exit_sentinel_exists(),
    };
}
