#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "i_video.h"
#include "v_video.h"

static FILE* headless_log;
static uint32_t frame_crc;
static int frame_count;

static uint32_t crc32_update(uint32_t crc, const byte* data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int)(crc & 1));
    }
    return ~crc;
}

static FILE* log_file(void)
{
    if (!headless_log)
    {
        const char* path = getenv("DOOM_HEADLESS_LOG");
        headless_log = fopen(path ? path : "qa/qa-smoke.log", "w");
        if (!headless_log)
            headless_log = stdout;
        fprintf(headless_log, "DOOM started (headless)\n");
        fflush(headless_log);
    }
    return headless_log;
}

void I_InitGraphics(void)
{
    (void)log_file();
}

void I_ShutdownGraphics(void)
{
    if (headless_log && headless_log != stdout)
    {
        fprintf(headless_log, "DOOM stopped after %d frames crc32=%08x\n", frame_count, frame_crc);
        fclose(headless_log);
    }
    headless_log = NULL;
}

void I_SetPalette(byte* palette)
{
    (void)palette;
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    frame_crc = crc32_update(frame_crc, screens[0], SCREENWIDTH * SCREENHEIGHT);
    ++frame_count;
    if ((frame_count % 32) == 0)
    {
        fprintf(log_file(), "frame=%d crc32=%08x\n", frame_count, frame_crc);
        fflush(headless_log);
    }
}

void I_ReadScreen(byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
}
