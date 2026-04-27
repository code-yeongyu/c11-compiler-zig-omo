#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct fopen_x_mode_result {
    int first_open_succeeded;
    int second_open_failed;
    int errno_was_eexist;
};

static void fopen_x_mode_path(char *buffer, size_t size)
{
    const char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = "/tmp";
    }
    (void)snprintf(buffer, size, "%s/c11_ref_fopen_x_%lu.tmp", tmpdir, (unsigned long)time(NULL));
}

struct fopen_x_mode_result fopen_x_mode_run(void)
{
    char path[256];
    FILE *first;
    FILE *second;
    int second_errno;

    fopen_x_mode_path(path, sizeof path);
    (void)remove(path);
    first = fopen(path, "wx");
    if (first == NULL) {
        return (struct fopen_x_mode_result){ .first_open_succeeded = 0, .second_open_failed = 0, .errno_was_eexist = 0 };
    }
    errno = 0;
    second = fopen(path, "wx");
    second_errno = errno;
    if (second != NULL) {
        (void)fclose(second);
    }
    (void)fclose(first);
    (void)remove(path);

    return (struct fopen_x_mode_result){
        .first_open_succeeded = 1,
        .second_open_failed = second == NULL,
        .errno_was_eexist = second_errno == EEXIST,
    };
}
