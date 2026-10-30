/* SDK and hardware entry points the port has no real counterpart for yet.
 * Each says what the original did and what the stand-in does. */
#include "sdk.h"

#include <string.h>

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

/* ---- Movies (THP) --------------------------------------------------------
 * The original streamed THP (Motion-JPEG + ADPCM) files. Until a decoder is
 * written, every movie ends at once: Mpeg_Heartbeat() returning 0 makes
 * State_Movie move on to the next movie and then back to the game.
 * TODO: THP demuxer + JPEG decode into a GL texture, audio into the mixer. */

s32 Mpeg_Create(void) { return 1; }
s32 Mpeg_Destroy(void) { return 1; }
s32 Mpeg_StartMovie(const char *name, s32 volume, double volumeScale) {
    (void)volume; (void)volumeScale;
    Port_Log("movie %s skipped (THP playback not implemented)", name ? name : "(null)");
    return 1;
}
s32 Mpeg_StopMovie(void) { return 1; }
s32 Mpeg_Heartbeat(void) { return 0; }
s32 Mpeg_Render(void) { return 1; }
