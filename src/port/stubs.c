/* SDK and hardware entry points the port has no real counterpart for yet.
 * Each says what the original did and what the stand-in does. */
#include "sdk.h"

#include <string.h>

/* ---- MSL rand() (see sdk.h) -------------------------------------------- */

static unsigned int s_randNext = 1;

#undef rand
#undef srand
int Port_Rand(void) {
    s_randNext = s_randNext * 1103515245u + 12345u;
    return (int)((s_randNext >> 16) & 0x7FFF);
}

void Port_SRand(unsigned int seed) { s_randNext = seed; }

/* ---- EXI / PPC ---------------------------------------------------------- */

/* Re-enabled the EXI2 (debugger) interrupt after DVD/card calls. */
s32 EnableEXI2Interrupts(void) { return 0; }

/* sync instruction: orders memory accesses for the hardware. */
void PPCSync(void) {}

/* ---- GBA link (not part of the port) ------------------------------------ */

void GBAConnectivityInit__Fv(void) {}
void GBAConnectivityLoop__Fv(void) {}

/* ---- IPL ROM font --------------------------------------------------------
 * The original copied the console's built-in ANSI font out of the IPL ROM.
 * That font isn't ours to ship; the header is left zeroed (no glyphs), so the
 * debug text printer (Render_Print) draws nothing.
 * TODO: build an equivalent font sheet at startup (e.g. from SDL_ttf). */

BOOL OSInitFont(struct OSFontHeader *fontData) {
    memset(fontData, 0, 0x20);
    return FALSE;
}

char *OSGetFontTexture(char *string, void **image, s32 *x, s32 *y, s32 *width) {
    *image = NULL;
    *x = *y = 0;
    *width = 0;
    return *string ? string + 1 : string;
}

/* Movies (THP): src/port/thp.c */
