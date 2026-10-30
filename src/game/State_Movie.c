#include "common.h"


/* the movie list to play back, and how far into it the next name starts */
u8 g_buttonUpCnt;
u32 g_moviePlaybackName;
u32 g_curMovieIndex;
s32 g_returnState = 4;

static char *StateMovie_GetNextMovieName(void) {
	/* the name is copied out of the list one character at a time */
	static char buffer[32];
		char *src;
	char *name;
		u32 n;
	u32 len;

	n = 0;
	if (g_moviePlaybackName == 0) {
		return NULL;
	}
	len = strlen((char *)g_moviePlaybackName);
	if (g_curMovieIndex >= len) {
		return NULL;
	}
	src = (char *)g_moviePlaybackName + g_curMovieIndex;
	while (g_curMovieIndex < len) {
		if (*src != ',') {
			buffer[n++] = *src;
		} else {
			g_curMovieIndex += 1;
			break;
		}
		src += 1;
		g_curMovieIndex += 1;
	}
	buffer[n] = 0;
	return buffer;
}


void StateMovie_SetMovieToPlay(s32 arg0) {
	g_moviePlaybackName = arg0;
}

s32 StateMovie_Init(s32 returnState) {
	M2C_UNK Mpeg_Create();
	M2C_UNK Mpeg_StartMovie();
	AudioSettings *audio;

	(void) 0;

	audio = GameSettings_GetAudioSettings();
	ProcessManager_Init();
	ProcessManager_Add("TimeHandler", 0, TimeHandler_Heartbeat, 0, 0);
	ProcessManager_Add("Input", Input_Init, Input_Heartbeat, 0, 0);
	g_curMovieIndex = 0;
	Mpeg_Create();
	Mpeg_StartMovie(StateMovie_GetNextMovieName(), audio->musicVolume, audio->musicVolume / 100.0f);
	g_returnState = returnState;
	g_buttonUpCnt = 0;
	return 1;
}

s32 StateMovie_Shutdown(void) {
    StateGameManager_RefreshMCPTextures();
    return 1;
}

s32 StateMovie_Heartbeat(void) {
	M2C_UNK Mpeg_Heartbeat();
	M2C_UNK Mpeg_StopMovie();
	M2C_UNK Mpeg_Destroy();
	M2C_UNK Mpeg_Create();
	M2C_UNK Mpeg_StartMovie();
	M2C_UNK Mpeg_Render();
	s8 skip;

	ProcessManager_Heartbeat(0);
	skip = (s8)Input_KeyPress(0, 9) || (s8)Input_KeyPress(0, 0);
	if (!(s8)Mpeg_Heartbeat() || (skip && g_buttonUpCnt > 10)) {
		char *name = StateMovie_GetNextMovieName();

		Mpeg_StopMovie();
		if (name != NULL) {
			AudioSettings *audio = GameSettings_GetAudioSettings();

			Mpeg_Destroy();
			Mpeg_Create();
			Mpeg_StartMovie(name, audio->sfxVolume, audio->sfxVolume / 100.0f);
			g_buttonUpCnt = 0;
		} else {
			g_moviePlaybackName = 0;
			Mpeg_Destroy();
			StateManager_QueueChange(g_returnState);
			return 1;
		}
	} else if (!skip) {
		g_buttonUpCnt++;
	} else {
		g_buttonUpCnt = 0;
	}
	Mpeg_Render();
	return 1;
}

