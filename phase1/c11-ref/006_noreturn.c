#include <stdnoreturn.h>
#include <stdlib.h>

noreturn void noreturn_leave_with(int code)
{
    exit(code);
}

int noreturn_function_is_addressable(void)
{
    void (*handler)(int) = noreturn_leave_with;
    return handler != 0;
}
