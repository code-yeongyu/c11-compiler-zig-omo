#include "test_support.h"
#include "006_noreturn.c"

int main(void)
{
    /* given */
    const int expected_addressable = 1;

    /* when */
    const int addressable = noreturn_function_is_addressable();

    /* then */
    assert(addressable == expected_addressable);
    C11_REF_OK();
}
