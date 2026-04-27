/* §6.7.2.4: the atomic type specifier takes a scalar/struct/union type name, not an array type. */
#include <stdatomic.h>

_Atomic(int[2]) invalid_atomic_array;

int main(void)
{
    return 0;
}
