#include "common.h"



s32 g_playbackCache[0x1000];
u32 g_fileIOHandle = -1;
u32 g_playbackPos;
u32 g_playbackFinish;
s32 g_playbackStatus;
s32 g_pauseState;
s32 Playback_GetElement(void) {
    s32 element;
    u32 bytesRead;

    if (g_playbackPos == g_playbackFinish) {
        File_Close(g_fileIOHandle);
        g_fileIOHandle = -1;
        g_playbackStatus = 0;
        g_pauseState = 1;
        element = g_playbackCache[g_playbackPos];
    } else {
        element = g_playbackCache[g_playbackPos++];
        if (g_playbackPos == 0x1000) {
            bytesRead = File_Read(g_fileIOHandle, 0x4000, g_playbackCache);
            g_playbackPos = 0;
            if (bytesRead != 0x4000) {
                g_playbackFinish = bytesRead - 1;
            }
        }
    }
    return element;
}

void Playback_PutElement(s32 element) {
    g_playbackCache[g_playbackPos++] = element;
    if (g_playbackPos == 0x1000) {
        File_Write(g_fileIOHandle, 0x4000, g_playbackCache);
        g_playbackPos = 0;
    }
}
