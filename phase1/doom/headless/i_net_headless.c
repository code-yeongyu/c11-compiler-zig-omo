#include <stdlib.h>
#include <string.h>

#include "d_net.h"
#include "doomstat.h"
#include "i_system.h"
#include "i_net.h"

#ifndef DOOM_MALLOC
#define DOOM_MALLOC malloc
#endif

void I_InitNetwork(void)
{
    doomcom = DOOM_MALLOC(sizeof(*doomcom));
    if (!doomcom)
        I_Error("doomcom alloc failed");

    memset(doomcom, 0, sizeof(*doomcom));
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->consoleplayer = 0;
    doomcom->ticdup = 1;
    netbuffer = &doomcom->data;
}

void I_NetCmd(void)
{
    doomcom->remotenode = -1;
}
