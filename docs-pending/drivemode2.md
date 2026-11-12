# The PC's third handling mode (CarAInfo.driveMode 2) in HWVX-SDL

Branch `drivemode2`. Three pieces of text for three documents.

## For docs/pc-version/README.md, "Only on PC" (replaces the row, answers the open question)

| a third car handling mode (`CarAInfo+0x11 == 2`) | `CarA_PreMove` `0x40E956` (sets turbo time to 1000, then the controller `0x413540`); rescue loading `0x412DB0`/`0x412FD0`; suspension `0x416CD3`..`0x417CA8`; wind sound `0x40EF94`; `CarA_Init` `0x40D545` (flat torque curve, also on GC) | GC and PS2 (`CarA_PreMove` `0x25F91C`) have an empty `case 2`: no driver. **No shipped car uses it**: all 33 are mode 1 on GC, PC and PS2, nothing writes the byte, and the PC has no menu or registry option for it. HWVX-SDL offers it (Options, Handling) |

What mode 2 does (all verified against the PC asm; HWVX-SDL `src/game/CarA_HW.c`):

* **Controller** `0x413540`: `back` (Player 0x14, action 4) reverses with half the torque; `brake`
  (0x13) slows the wheel speed to 0 at `brakeDecel` (from reversing at `reverseThrottleDecel`)
  and never reverses; `gas` (0x12) adds the **full** torque (mode 1 takes half), shifts up past
  `upshiftRpm` with `CarA_ProcessImpulse(car, dir, pos, -10·dir, 1.0)` from 8 behind and 3 below
  the car (the "backfire": a kick forward, built like the turbo's); off the gas or at the rev limit
  on the ground, engine braking of `engineBrake·255·brakeDecel` plus `160·forward.y` uphill, with
  downshifts. Handbrake (0x15) sets flag 0x8000. Steering is immediate:
  `clamp((180 − forward speed)/150, accel+0x4C, 1) · stick/128 · −π/4`; flag 0x2000 holds it at 0
  for 100 ms after a collision. No freestyle stunts.
* **Rescue loading** `0x412DB0`: under speed 30 with button 0 pressed, inside an enabled creature
  volume (type 5) whose data's first byte is set: flag 0x200, the creatures riding in the car's
  pods (type 13 slaves) tagged with the volume; always flag 0x80 (interface condition 3) until the
  car moves off (`0x412FD0`). From Armageddon's engine; no HWVX car carries pods.
* **Suspension** `0x416A40`: below `accel+0x40` speed the ride height eases to `axis[1].y`, below
  `accel+0x44` the dampers scale to `accel+0x48`; off the gas the wheels' drag winds the rpm down
  by `coastRpmScale` (on the gas not at all); grip × |ground normal.y|³; skid marks from a slip of
  3 (not 110) while braking or on the handbrake.
* **Wind sound** `0x40EF94`: pitch `|rpm/maxRpm · gears[1].soundSet.pitchScale| + 0.01` (mode 1:
  `·3 + 0.5`).
* `accel+0x40..0x4C` are past `CarAAccel`'s 0x3C bytes: the PC reads whatever follows it in the
  pack, `0, 0, 1, 0` after every car on GC and PC alike, so the ride-height and damper paths never
  apply and steering fades to nothing at 180. HWVX-SDL takes these values as constants
  (`g_carAMode2Tuning`) instead of reading out of bounds.
* No controller layout (GC/PC) binds action 4, and the PC keyboard has no key for it: on the PC a
  human in a mode 2 car could not reverse. The AI does (`AI_CarA`: button 14 when stuck).

## For docs/xdl-format/README.md §11.1 (or §11.4: the save's settings)

The byte after the audio settings (`GeneralGameSettings.audio`, +3; every release writes 0 and
none reads it) holds HWVX-SDL's **handling** choice: 0 the cars' own (`CarAInfo.driveMode`), 2 the
PC's third mode. In a JSON save it is `"settings": {"handling": 2}`, written only when not 0;
JSON saves from before it named the byte `audio_unused`, which is still read. A binary save with
it set still loads on the releases (they ignore the byte).

## For docs/fields/README.md, "PC-only data uses" (extends the CarAAccel bullet)

* CarAInfo 0x11 `driveMode`: 1 in every car (GC, PC, PS2). A mod may set 2 (the PC's third
  mode, on every release in HWVX-SDL) or 0 (no driver). HWVX-SDL also turns every mode 1 car to 2
  when the player picks Options > Handling > PC Mode 2 (`CarA.driveMode`, decided in `CarA_Init`;
  never online, in the attract demo or in a recorded demo's playback).
* The PC's mode 2 reads CarAAccel **0x40-0x4C**, past the struct: `0, 0, 1, 0` in every car's
  pack (the bytes after the accel record), on GC and PC alike.

## Release differences and the choice made

GC and PS2 agree (no mode 2 driver); the PC has one. No data uses it on any release, so the
default behaviour is the same everywhere and stays retail: the attract demos are frame-identical
to pc-data on GC, PC and PS2 data, also with `HWVX_HANDLING=2` (demos keep the cars' own
handling). The option is the port's, on all three releases' Options screens, since the code is
data-independent and the data it reads is the same on all three.
