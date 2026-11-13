# World music: when a level's song starts (for HWVX-SDL/README.md, the testing / HWVX_AUDIODUMP notes)

A level's MIDI song is created and set playing at level start, but every retail
level song (GameCube, PC and PS2 alike) begins with a silent hold loop: marker 1
(CC 0x6E 1) followed, 960 ticks later, by CC 0x70 1 ("jump to marker 1"). The
music proper starts only when the level's script orders it: the LevelStart
trigger plays the intro interface (the objectives box with "Continue", e.g.
8086EE40 in joyride world1); its InterfaceEnd trigger unlocks the cars, turns
the HUD on and, 500 ms later, runs MusicChange { orderMode 41 (jump), order 31 }
(challenge ch1_l2: marker 2; drag: 12). So a run with no input sits on the intro
box with the song looping silently: an HWVX_AUDIODUMP of it has no music. A test
that needs the music presses A first, e.g.
`HWVX_START=race:mode=joyride,world=world1 HWVX_AUTOINPUT="+240:a,+60:a"`.
Same for the viewer's `play song`: it plays the hold loop (no voices) unless a
marker is ordered.

`xdlrs audio level DUMP.f32 [--window S] [--from S]` prints an audio dump's
RMS per window, total and in three bands (<250 Hz, 250 Hz-4 kHz, >4 kHz).
Measured (35 s runs, A pressed, sound effects silenced in a debug build, so music
alone): joyride world1 about -30 to -35 dBFS on GC, PC and PS2; challenge ch1_l2,
adventure, battle and drag -27 to -38 dBFS; the front end's HW_music about -28.
