#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#define __STDC_WANT_LIB_EXT1__ 1

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

ssize_t getline(char **restrict lineptr, size_t *restrict n, FILE *restrict stream);

struct gets_removal_result {
    int gets_macro_absent;
    int getline_read_bytes;
    int annex_k_gets_s_available;
};

static int gets_removal_annex_k_available(void)
{
#if defined(__STDC_LIB_EXT1__)
    char *(*function)(char *, rsize_t) = gets_s;
    return function != NULL;
#else
    return 0;
#endif
}

struct gets_removal_result gets_removal_run(void)
{
    char *line = NULL;
    size_t capacity = 0u;
    FILE *stream = tmpfile();
    int read_bytes = -1;

    if (stream != NULL) {
        (void)fputs("c11\n", stream);
        rewind(stream);
        read_bytes = (int)getline(&line, &capacity, stream);
        (void)fclose(stream);
    }
    free(line);

    return (struct gets_removal_result){
#ifdef gets
        .gets_macro_absent = 0,
#else
        .gets_macro_absent = 1,
#endif
        .getline_read_bytes = read_bytes,
        .annex_k_gets_s_available = gets_removal_annex_k_available(),
    };
}
