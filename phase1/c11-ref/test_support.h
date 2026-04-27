#ifndef C11_REF_TEST_SUPPORT_H
#define C11_REF_TEST_SUPPORT_H

#include <assert.h>
#include <stdio.h>

#define C11_REF_OK() \
    do { \
        puts("OK"); \
        return 0; \
    } while (0)

#endif
