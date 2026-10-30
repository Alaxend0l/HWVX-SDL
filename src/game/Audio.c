#include "common.h"

/* The sound system's update hooks, installed by the create calls below. */
void (*volatile SongUpdate)(void);
void (*volatile VoiceUpdate)(void);

/* Set once Audio_Create() has brought everything up; it makes later calls no-ops. */
s8 g_init;



s8 Audio_Create(void)
{
	if (!g_init)
	{
		VoiceUpdate = NULL;
		SongUpdate = NULL;

		if (AudioMusic_Create() && AudioIT_Create() && AudioMIDI_Create() && AudioVoice_Create() && Audio_Create_())
		{
			g_init = 1;
		}
	}

	return g_init;
}

s8 Audio_Init(void)
{
			VoiceUpdate = 0;
	SongUpdate = NULL;

	if (AudioMusic_Init() && AudioIT_Init() && AudioMIDI_Init() && AudioVoice_Init() && Audio_Init_())
	{
		 return 1;
	}

	return 0;
}

void Audio_Destroy(void)
{
	SongUpdate = VoiceUpdate = NULL;

	AudioIT_Destroy(0);
	AudioMIDI_Destroy();
	AudioVoice_Destroy();
	Audio_Destroy_();

	g_init = 0;
}

void Audio_Update(u32 unused)
{
	if (SongUpdate != NULL)
	{
		SongUpdate();
	}

	if (VoiceUpdate != 0)
	{
		((void (*)(void))VoiceUpdate)();
	}
}
