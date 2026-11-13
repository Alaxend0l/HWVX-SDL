# Stereo pan: GameCube/PS2 vs PC (for docs/pc-version, and README "Command line")

A player's report: the PC release has wider stereo than the GameCube. It does, and the cause is
only the last step, the speaker law. All three releases compute the same pan:

- SFX_ProcessSource (GC Voice/AudioManager; PC 0x409830..0x409A01; PS2 EE 0x257534): pan =
  26213.6 (0x46CCCB33) x the source's x in the listener's frame (normalized), so +-0x6666 at the
  side; split screen (more than one listener) centres everything. The IT player: channel pan
  (p - 32) << 10, +-0x7FFF (GC IT.c, PC 0x45B86A). SetVoicePos stores it at Voice+0x40 (PC 0x459A70).
- GameCube SetVoiceVol_ (0x800BD1D4) and PS2 AUDIO.IRX SetVoiceVol_ (IOP 0x5458): 7-bit index
  ((pan >> 9) & 0x7F) ^ 0x40 into g_panTab = 32767 sqrt(i/127) (the same table on both):
  equal power, -3 dB per speaker at centre, 9.8 dB apart at the side; music at full pan is hard.
  No AXSetMode in the GC binary: plain stereo AX, no Dolby Pro Logic II; the port's ax.c mixes
  vL/vR linearly, as AX does.
- PC SetVoiceVol_ (0x45DC20): SetVolume(600 log2(vol) - 9000) (0x45D5D0, an fyl2x; the decomp's
  C for it lacks the log) and SetPan(pan / 4) (0x45D610), clamped +-10000: the far speaker
  -|pan|/400 dB, the near one at full. 30 deg off centre: 32.8 dB apart (GC 3.7); at the side
  65.5 dB (GC 9.8); a music channel at 75 % pan 41 dB (GC 4.9). No speaker-mode check, no reverb.

`xdlrs audio pan` prints the table; `xdlrs audio level DUMP --lr` a dump's L/R per window.
Measured (HWVX_START=race:mode=battle, world 0, A presses, 0.25 s windows, from 5 s, music on): mean |R-L|
1.6 dB (max 4.0) with the GameCube law, 2.7 dB (max 6.5) with --pc-pan; the song and the
player's own (centred) engine dominate the mix, single sounds differ by the amounts above.

Majority rule: GC and PS2 agree, so the port keeps their law by default. `--pc-pan` /
`HWVX_PC_PAN=1` (Port_PCPan, game/Voice_.c PCPan) uses the PC law for every voice (effects and
music), with the centre kept at the GameCube level (near speaker 0x5A82), so centred sounds and
the attract demo are unchanged; a sound at the side is 3 dB lower in total power, as on the PC.

Launcher (stereo2): the Settings page's last row, "Stereo: GameCube/PS2 | PC (wider)", saved as
`stereo=gc|pc` in graphics.cfg (default gc); `--pc-pan` and `HWVX_PC_PAN=1|0` override it for one
run (main_sdl.c Port_PCPan, cached at start) and are not saved. The Settings rows are 34 px apart
now (8 rows fit above the footer).
