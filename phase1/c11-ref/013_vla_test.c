#include "test_support.h"
#include "013_vla.c"

int main(void)
{
    /* given */
    const int vla_available =
#if defined(__STDC_NO_VLA__)
        0;
#else
        1;
#endif

    /* when */
    const struct vla_result result = vla_run();

    /* then */
    assert(result.supported == vla_available);
    assert(result.sum == (vla_available ? 34 : 15));
    C11_REF_OK();
}
