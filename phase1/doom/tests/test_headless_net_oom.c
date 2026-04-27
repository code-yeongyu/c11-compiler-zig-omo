#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "d_net.h"

doomcom_t* doomcom;
doomdata_t* netbuffer;

static jmp_buf error_jmp;
static char error_message[64];

void* fail_malloc(size_t size)
{
    (void)size;
    return 0;
}

void I_Error(char* error, ...)
{
    va_list args;

    va_start(args, error);
    vsnprintf(error_message, sizeof(error_message), error, args);
    va_end(args);
    longjmp(error_jmp, 1);
}

#define DOOM_MALLOC fail_malloc
#include "../headless/i_net_headless.c"

int main(void)
{
    // given malloc fails during headless network startup
    if (setjmp(error_jmp) == 0) {
        // when network initialization tries to allocate doomcom
        I_InitNetwork();

        // then startup must not continue into a null doomcom dereference
        fprintf(stderr, "expected I_Error on doomcom allocation failure\n");
        return EXIT_FAILURE;
    }

    // then the DOOM error path reports the allocation failure
    if (strcmp(error_message, "doomcom alloc failed") != 0) {
        fprintf(stderr, "unexpected error message: %s\n", error_message);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
