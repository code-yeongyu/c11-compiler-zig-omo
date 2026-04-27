#include <stdlib.h>
#include <string.h>

#include "d_net.h"
#include "doomstat.h"
#include "i_net.h"

void I_InitNetwork(void)
{
    doomcom = malloc(sizeof(*doomcom));
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
