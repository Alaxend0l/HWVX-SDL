# The PS2 prototype in PCSX2: what the real one does (reference for the port)

Goes with `prototype-worlds.md`, under docs/xdl-format/README.md §9.1.1 ("Tested" and "Doubtful").
Its Ripper Wheels paragraph is corrected below.

## How it was captured

The user played the prototype ("Hot Wheels Velocity X Early Code 16th July 2002") in their own
PCSX2 2.8.2 (Vulkan renderer: accurate; the Metal renderer has emulation issues). Screenshots
were taken alongside, without touching PCSX2 itself:
* `winid` (Swift, CoreGraphics `CGWindowListCopyWindowInfo`) finds the PCSX2 window;
* `screencapture -x -o -l <window id>` takes it, one shot every 3 s;
* it stops when the window is gone.

That reads the screen only: no settings, input or memory cards. 167 shots in about 8 minutes
(scratch, not in the repo). The game imagery stays on the player's machine.

## Findings

* **Prototype priority.** The user's view: the prototype is far worse than the finished game. Of
  its content only the three unused cars (Fandango, Sweet 16 II, Jester) are worth having. The
  rest is research. The port matches this: off by default; mode "cars" is the one for playing;
  "added" and "replace" are for research. Nothing of it changes the plain release.
* **Car look.** Fandango in a race is orange all over, the roof too, with its logo on the sides.
  The port's black shell was its own bug (the prototype's shadow hull drawn as geometry, fixed
  by leaving it out), not the prototype's look.
* **x/y/z readout.** The prototype's race HUD shows the car's position, live, in every race. In
  the port it is the option `HWVX_PROTO_DEBUGHUD`, off by default.
* **AI in drag races.** Opponents (Overboard, Power Rocket, HW Prototype 12; "Round 1/3") never
  turn back. If the port's do on a prototype world, it's the nav conversion (`prototype-worlds.md`,
  nav points), not the prototype.
* **Ripper Wheels.** Never selectable and never unlocked in play, even with Game Settings →
  "Debug Unlock All" On. So it is not only that a new game starts with items 0–5 unlocked
  (`GameSetting_Init`): with everything unlocked, the prototype's item select still doesn't
  offer it. Probably that screen's own bug or a gap; the screen's code and data weren't read.
  The port uses the release's item select, so it changes nothing there.
* **Screens the port doesn't use.** The port keeps the release's for both, as the user prefers:
  * the prototype's pause menu (Continue / Mission Info On-Off / Restart / Controller / Quit,
    mission text over a tinted scene);
  * its briefings (video portraits, scrolling text: "Wait, hold on, Max… sensors have detected
    explosives in the engineering plant").
* **Other screens seen.** Game Settings (Controller Settings, Cheats, Game Difficulty, music and
  SFX volume, Debug Unlock All); its own car select (36 cars, Speed/Armor/Agility/Grip bars);
  "Options Not Implemented" (Rounds 2/3, Gadgets, Opponents); the drag results summary.
* **Stability.** The user had a crash in an earlier session (prototype known to be unstable).
  This session ended by the user quitting, not a crash.

## For the user to check, if wanted

* Where the earlier crash happened (screen, action), to record it as the prototype's own.
