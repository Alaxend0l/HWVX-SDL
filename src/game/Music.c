#include "common.h"

/* main/Music.  Song state: the two raw volumes are combined into the one the
 * player uses, and every per-track hook starts out as AudioVoid. */
extern u32 g_musicVol1;
extern u32 g_musicVol2;
extern u32 g_musicVol;
u8 g_musicTitle[0x20];

u32 g_musicVol1;
u32 g_musicVol2;
u32 g_musicVol;
u32 g_trkOn;
u32 g_trkOff;
s32 g_musicState;
s32 (*DestroyMusic)(void);
s32 (*SetMusicState)(void);
s32 (*SetMusicVolume)(void);
s32 (*SetMusicPitch)(void);
s32 (*SetMusicTempo)(void);
s32 (*SetMusicOrder)(void);
s32 (*SetMusicTrackPriority)(void);
s32 (*SetMusicTrackOn)(void);
s32 (*SetMusicTrackOff)(void);
s32 (*SetMusicTrackVol)(void);

/* MIDI.c / IT.c entry points come from game_protos.h; the table above
   stores them as s32 (*)(void), as the original did. */


s32 AudioMusic_Create(void) {
  extern s32 g_musicState;

  g_musicVol = 0x7FFF;
  g_musicVol1 = 0x7FFF;
  g_musicVol2 = 0x7FFF;
  g_musicState = 0;
  g_musicTitle[0x1F] = 0;
  return 1;
}

s32 AudioMusic_Init(void) {

  DestroyMusic = AudioVoid;
  SetMusicState = AudioVoid;
  SetMusicVolume = AudioVoid;
  SetMusicPitch = AudioVoid;
  SetMusicTempo = AudioVoid;
  SetMusicOrder = AudioVoid;
  SetMusicTrackPriority = AudioVoid;
  SetMusicTrackOn = AudioVoid;
  SetMusicTrackOff = AudioVoid;
  SetMusicTrackVol = AudioVoid;
  memset(g_musicTitle, 0, 0x20);
  return 1;
}



void SetMusicVol(u32 vol)
{
	g_musicVol1 = vol;
	g_musicVol = g_musicVol1 * g_musicVol2 >> 15;
	SetMusicVolume();
}
void SetMusicGVol(u32 vol) {
  g_musicVol2 = vol;
  g_musicVol = (g_musicVol1 * g_musicVol2) >> 15;
  SetMusicVolume();
}


s32 AudioVoid(void) { return 0; }


void CreateMusic(u8 *data, u8 *bank) {
  s8 CmpType_(u8 * chunk, s32 type), CmpID_(u8 * chunk, s32 id);

  if (data == 0 || bank == 0) {
    return;
  }
  /* port: the MIDI/IT players read big-endian RIFX song data directly and
   * aren't byte-swapped yet; play no music rather than misread it. */
  {
    static int warned;
    if (!warned) {
      warned = 1;
      OSReport("music disabled: RIFX song data not byte-swapped yet\n");
    }
    return;
  }
  if (CmpType_(data, 'MIDI')) {
    DestroyMusic = DestroyMIDI;
    SetMusicState = SetMIDIState;
    SetMusicVolume = SetMIDIVol;
    SetMusicPitch = SetMIDIPitch;
    SetMusicTempo = SetMIDITempo;
    SetMusicOrder = SetMIDIMarker;
    SetMusicTrackPriority = SetMIDITrackPriority;
    SetMusicTrackOn = SetMIDITrackOn;
    SetMusicTrackOff = SetMIDITrackOff;
    SetMusicTrackVol = SetMIDITrackVol;
    (CreateMIDI)(data, bank);
  } else if (CmpID_(data, 'IMPM')) {
    (DestroyMusic) = DestroyIT;
    (SetMusicState) = SetITState;
    (SetMusicVolume) = SetITVol;
    (SetMusicPitch) = SetITPitch;
    (SetMusicTempo) = SetITTempo;
    (SetMusicOrder) = SetITOrder;
    (SetMusicTrackPriority) = SetITTrackPriority;
    (SetMusicTrackOn) = SetITTrackOn;
    (SetMusicTrackOff) = SetITTrackOff;
    (SetMusicTrackVol) = SetITTrackVol;
    (CreateIT)(data, bank);
  }
}

