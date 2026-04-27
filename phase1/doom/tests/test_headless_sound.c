#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sounds.h"
#include "w_wad.h"

extern int I_GetSfxLumpNum(sfxinfo_t* sfxinfo);

static char requested_lump[9];

int W_GetNumForName(char* name)
{
    strncpy(requested_lump, name, sizeof(requested_lump) - 1);
    requested_lump[sizeof(requested_lump) - 1] = '\0';
    return 42;
}

int W_CheckNumForName(char* name)
{
    (void)name;
    return -1;
}

int main(void)
{
    // given a headless sound effect named like the original DOOM table
    sfxinfo_t pistol = { "pistol", 0, 64, 0, 0, 0, 0, 0, -1 };

    // when the backend resolves its WAD lump number
    int lump = I_GetSfxLumpNum(&pistol);

    // then it preserves DOOM's ds-prefixed lump lookup contract
    if (lump != 42 || strcmp(requested_lump, "dspistol") != 0) {
        fprintf(stderr, "expected dspistol lookup, got lump=%d name=%s\n", lump, requested_lump);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
