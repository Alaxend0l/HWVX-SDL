# Car stats inventory (0.4.1 part C, stage C1)

For docs/xdl-format/README.md, next to the car parts (CarAHud) section, and for the C3 analysis.
Made with `xdlrs cars stats DIR... [--md OUT] [--dump OUT.json]` (tools/xdlrs/src/cars.rs). It reads
`HW_Cars` and `HW_IPack` of each release folder, read only (the PS2 prototype's archive version
177/179 layout included). The tables below are its output for GC `orig/files`, PC, PS2 and the
PS2 prototype (`ps2beta`).

## Findings

* **Roles → bytes → labels** (all three releases): role 70 = `stats[0]` **SPEED**, 71 = `[1]`
  **ARMOR**, 72 = `[2]` **GRIP**, 73 = `[3]` **STUNT**. The labels are text objects next to the
  bars (car select, interface header id 2, objects 56/59/62/65; HW_IPack interface_3, _6 and _7
  hold the same screen). The prototype uses the same roles and bytes and calls role 73 **AGILITY**.
* **Bar curve: linear, 0-100, clamped.** InterfaceManager sets each track's time to the byte
  (reset, then dt = value), so a byte is a time in a 100-unit animation. Retail bar (image):
  scale x 0 → 85 over 0..100 (0.85 units per point) and its UV right edge 0.25 → 0.996 over the
  same range, so the texture is cropped, not stretched. A number object beside it prints the
  byte (`%d`), moves x 25 → 110 (right column 485 → 570) and fades black → yellow (0..1) →
  white (1..100). Past 100 the tracks hold their last key (no active key loops them): **255
  draws like 100**, so the usable range is 0-100. Prototype: vertical bars (rot ±90), scale y
  9 → 90 over 0..100, no number.
* **Retail bytes:** used range 25-100; the
  "floor" is 30 for most cars (25 for a few). The bytes are **identical on GC, PC and PS2** for all 33
  cars. The prototype's 33 twin cars carry **the same bytes as their release twin**; its three
  own cars: Fandango 30/50/45/30, Sweet 16 II 52/30/30/35, Jester 51/30/50/60.
* **Physics differences between releases:** only car 10 (Hammered Coupe) on the PC: inertiaScale
  0.5 (GC/PS2 0.7), front handbrakeGrip 0.62 (0.69), rear restLength 1.1 (1.4). GC and PS2 agree
  (majority rule: theirs). Everything else listed below is equal in all three.
* **What actually varies per car** (input for C2/C3):
  * SPEED: only `maxRpm` (7100-10000) and `mass` (15-40) vary; torque curve (0.2 0.3 0.001 0),
    gear ratios (0 0.012 0.017 0.02 0.027), driveRpmScale 2, brakes, turbo scales are shared
    (exceptions: MS-T Suzuka coastRpmScale 0, turboTorque 1.25, maxGrade 220; the bus up/down
    6000/3500; maxGrade 290-300 on Rigor Motor, the bus, Arachnorod). So top speed should
    follow maxRpm closely; acceleration differs only by mass and maxRpm.
  * GRIP: per-wheel friction (front 2.3-4.7, rear 1.9-3.5), maxFriction, handbrakeGrip, driveBlend,
    springRate, damping, restLength, tractionLift (0.3-1), steerRate (6; Hyper Mite 7.5, bus 5).
    `CarAInfo.gripScale` (0 or 50) is read only by the AI (AI_CarA.c, autopilot), not the player's
    handling. driveMode is 1 in every car (mode 2 is the PC's option, not data).
  * ARMOR: maxHealth (1000-1700), damageThreshold (0-25), damageScale (0.04-0.15, bumper damage
    per closing speed: higher is weaker), maxImpactVel, mass; damagePercent 75 / damageLevel 20
    in every retail car.
  * STUNT: pitch = roll rate (1.8-2.6), spin = pitch + 0.6; scores, maxTime, turboPerPoint and
    fail times are the same in every car (bus: minFail 50). So STUNT can only reflect rotation
    speed (plan question 11).
* **Prototype physics** differ from their twins mostly in damage (damageScale 0.02, damagePercent
  99 / damageLevel 1, which Port_Proto.c sets to 75 / 20 at load), stunt scores (75/60/35, fail
  10..650) and a few tyre/box values (table at the end).
* Car names come from the car select's 36-entry list (index = car id; retail entries end in `*`).

## Bars on the car select (roles 70-73)

gc car select: interface_3 header 0x3B7780 (84 objects), interface_6 header 0x72D7A0 (84 objects), interface_7 header 0x72D860 (84 objects)

| object | role | carStats | label | kind | track | at 0 | 25 | 50 | 75 | 100 | 150 | 255 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 57 | 71 | [1] | ARMOR | number | pos x | 25 | 46.25 | 67.5 | 88.75 | 110 | 110 | 110 |
| 58 | 71 | [1] | ARMOR | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 60 | 70 | [0] | SPEED | number | pos x | 25 | 46.25 | 67.5 | 88.75 | 110 | 110 | 110 |
| 61 | 70 | [0] | SPEED | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 63 | 73 | [3] | STUNT | number | pos x | 485 | 506.25 | 527.5 | 548.75 | 570 | 570 | 570 |
| 64 | 73 | [3] | STUNT | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 66 | 72 | [2] | GRIP | number | pos x | 485 | 506.25 | 527.5 | 548.75 | 570 | 570 | 570 |
| 67 | 72 | [2] | GRIP | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |

pc car select: interface_3 header 0x14C300 (84 objects), interface_6 header 0x3A7AB0 (84 objects), interface_7 header 0x3A7B50 (84 objects)

| object | role | carStats | label | kind | track | at 0 | 25 | 50 | 75 | 100 | 150 | 255 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 57 | 71 | [1] | ARMOR | number | pos x | 25 | 46.25 | 67.5 | 88.75 | 110 | 110 | 110 |
| 58 | 71 | [1] | ARMOR | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 60 | 70 | [0] | SPEED | number | pos x | 25 | 46.25 | 67.5 | 88.75 | 110 | 110 | 110 |
| 61 | 70 | [0] | SPEED | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 63 | 73 | [3] | STUNT | number | pos x | 485 | 506.25 | 527.5 | 548.75 | 570 | 570 | 570 |
| 64 | 73 | [3] | STUNT | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 66 | 72 | [2] | GRIP | number | pos x | 485 | 506.25 | 527.5 | 548.75 | 570 | 570 | 570 |
| 67 | 72 | [2] | GRIP | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |

ps2 car select: interface_3 header 0x16FBB0 (84 objects), interface_6 header 0x5AE430 (84 objects), interface_7 header 0x5AE4D0 (84 objects)

| object | role | carStats | label | kind | track | at 0 | 25 | 50 | 75 | 100 | 150 | 255 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 57 | 71 | [1] | ARMOR | number | pos x | 25 | 46.25 | 67.5 | 88.75 | 110 | 110 | 110 |
| 58 | 71 | [1] | ARMOR | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 60 | 70 | [0] | SPEED | number | pos x | 25 | 46.25 | 67.5 | 88.75 | 110 | 110 | 110 |
| 61 | 70 | [0] | SPEED | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 63 | 73 | [3] | STUNT | number | pos x | 485 | 506.25 | 527.5 | 548.75 | 570 | 570 | 570 |
| 64 | 73 | [3] | STUNT | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |
| 66 | 72 | [2] | GRIP | number | pos x | 485 | 506.25 | 527.5 | 548.75 | 570 | 570 | 570 |
| 67 | 72 | [2] | GRIP | bar | scale x | 0 | 21.25 | 42.5 | 63.75 | 85 | 85 | 85 |

proto car select: interface_1 header 0x398DE0 (81 objects), interface_3 header 0x183720 (81 objects)

| object | role | carStats | label | kind | track | at 0 | 25 | 50 | 75 | 100 | 150 | 255 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 48 | 73 | [3] | AGILITY | bar | scale y | 9 | 29.25 | 49.5 | 69.75 | 90 | 90 | 90 |
| 50 | 72 | [2] | GRIP | bar | scale y | 9 | 29.25 | 49.5 | 69.75 | 90 | 90 | 90 |
| 57 | 70 | [0] | SPEED | bar | scale y | 9 | 29.25 | 49.5 | 69.75 | 90 | 90 | 90 |
| 59 | 71 | [1] | ARMOR | bar | scale y | 9 | 29.25 | 49.5 | 69.75 | 90 | 90 | 90 |

proto car select: interface_1 header 0x3E5400 (38 objects)

| object | role | carStats | label | kind | track | at 0 | 25 | 50 | 75 | 100 | 150 | 255 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 15 | 73 | [3] | AGILITY | bar | scale y | 9 | 29.25 | 49.5 | 69.75 | 90 | 90 | 90 |
| 18 | 72 | [2] | GRIP | bar | scale y | 9 | 29.25 | 49.5 | 69.75 | 90 | 90 | 90 |
| 24 | 70 | [0] | SPEED | bar | scale y | 9 | 29.25 | 49.5 | 69.75 | 90 | 90 | 90 |
| 27 | 71 | [1] | ARMOR | bar | scale y | 9 | 29.25 | 49.5 | 69.75 | 90 | 90 | 90 |

## CarAHud.stats (the four bytes)

Releases read: gc, pc, ps2. Columns are carStats[0..3].

| car | name | SPEED [0] | ARMOR [1] | GRIP [2] | STUNT [3] | releases |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Twin Mill 2 | 44 | 30 | 30 | 30 | same |
| 1 | At-A-Tude | 35 | 40 | 50 | 35 | same |
| 2 | Overbored 454 | 44 | 30 | 55 | 40 | same |
| 3 | MS-T Suzuka | 44 | 30 | 40 | 30 | same |
| 4 | Super Tuned | 30 | 40 | 50 | 30 | same |
| 5 | Sooo Fast | 36 | 30 | 65 | 30 | same |
| 6 | Flashfire | 46 | 30 | 40 | 40 | same |
| 7 | Mega-Duty | 30 | 60 | 60 | 30 | same |
| 8 | Muscle Tone | 42 | 30 | 60 | 40 | same |
| 9 | Sho-Stopper | 50 | 30 | 40 | 30 | same |
| 10 | Hammered Coupe | 38 | 40 | 40 | 40 | same |
| 11 | Jaded | 62 | 30 | 50 | 60 | same |
| 12 | Way 2 Fast | 60 | 30 | 35 | 30 | same |
| 13 | Deora II | 30 | 90 | 50 | 30 | same |
| 14 | Hyper Mite | 45 | 30 | 100 | 50 | same |
| 15 | Power Pistons | 62 | 30 | 25 | 40 | same |
| 16 | Evil Twin | 44 | 50 | 40 | 30 | same |
| 17 | Surf Crate | 36 | 30 | 55 | 40 | same |
| 18 | Rigor Motor | 68 | 30 | 40 | 35 | same |
| 19 | Lakester | 50 | 60 | 60 | 50 | same |
| 20 | Splittin' Image 2 | 62 | 30 | 50 | 60 | same |
| 21 | Greased Lightnin' | 70 | 30 | 30 | 40 | same |
| 22 | Hyperliner | 36 | 80 | 50 | 40 | same |
| 23 | Maelstrom | 58 | 30 | 60 | 70 | same |
| 24 | Sol-Aire CX4 | 76 | 30 | 30 | 45 | same |
| 25 | Surfin' School Bus | 44 | 100 | 40 | 25 | same |
| 26 | Vulture | 80 | 30 | 50 | 90 | same |
| 27 | Arachnorod | 92 | 30 | 25 | 50 | same |
| 28 | Krazy 8; | 68 | 75 | 30 | 45 | same |
| 29 | Shadow Jet | 66 | 30 | 60 | 80 | same |
| 30 | HW Prototype 12 | 100 | 30 | 25 | 60 | same |
| 31 | Power Rocket | 82 | 65 | 40 | 50 | same |
| 32 | Jet Threat 3.0 | 90 | 30 | 35 | 100 | same |

## SPEED: engine, gears, drive

torque curve, rpm limits, gear ratios (wheel speed per rpm), drive friction, brakes, 4WD turbo, mass. One value: all of gc/pc/ps2 agree; else per release.

| car | name | maxRpm | up | down | torqueCurve (t0 tMax s0 sMax) | gears (ratio) | driveRpmScale | coastRpmScale | brakeDecel | engineBrake | turboRpm | turboTorque | maxGrade | mass |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Twin Mill 2 | 7600 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 1 | At-A-Tude | 7100 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 2 | Overbored 454 | 7600 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 3 | MS-T Suzuka | 7600 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 0 | 1 | 0.25 | 1.25 | 1.25 | 220 | 20 |
| 4 | Super Tuned | 7100 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 5 | Sooo Fast | 7400 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 6 | Flashfire | 7700 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 7 | Mega-Duty | 7100 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 25 |
| 8 | Muscle Tone | 7500 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 9 | Sho-Stopper | 7900 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 10 | Hammered Coupe | 7500 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 11 | Jaded | 8400 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 12 | Way 2 Fast | 8300 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 13 | Deora II | 7600 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 14 | Hyper Mite | 7700 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 15 |
| 15 | Power Pistons | 8400 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 16 | Evil Twin | 7600 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 17 | Surf Crate | 7400 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 18 | Rigor Motor | 8700 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 300 | 20 |
| 19 | Lakester | 7900 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 20 | Splittin' Image 2 | 8400 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 21 | Greased Lightnin' | 8800 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 22 | Hyperliner | 7400 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 23 | Maelstrom | 8300 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 24 | Sol-Aire CX4 | 9100 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 25 | Surfin' School Bus | 7600 | 6000 | 3500 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 300 | 40 |
| 26 | Vulture | 9300 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 27 | Arachnorod | 9700 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 290 | 20 |
| 28 | Krazy 8; | 8700 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 29 | Shadow Jet | 8600 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 30 | HW Prototype 12 | 10000 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 31 | Power Rocket | 9400 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |
| 32 | Jet Threat 3.0 | 9700 | 6500 | 4000 | 0.2 0.3 0.001 0 | 0 0.012 0.017 0.02 0.027 | 2 | 1.5 | 1 | 0.25 | 1.25 | 1.15 | 10000 | 20 |

## GRIP: tyres, steering, suspension

wheels: front pair / rear pair when they match, else all four; gripScale is the AI's (AI_CarA.c). One value: all of gc/pc/ps2 agree; else per release.

| car | name | driveMode | steerRate | tractionLift | friction | maxFriction | handbrakeGrip | driveBlend | springRate | damping | restLength | inertiaScale | gripScale (AI) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Twin Mill 2 | 1 | 6 | 0.5 | 2.35 / 1.95 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 0 |
| 1 | At-A-Tude | 1 | 6 | 0.5 | 2.7 / 2.1 | 1500 / 1500 | 0.65 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 2 | Overbored 454 | 1 | 6 | 1 | 2.75 / 2.15 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 3 | MS-T Suzuka | 1 | 6 | 0.5 | 2.4 / 2 | 2000 / 1500 | 0.75 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1.2 / 1.2 | 0.5 0.5 0.5 | 50 |
| 4 | Super Tuned | 1 | 6 | 0.35 | 2.7 / 2.1 | 1500 / 1500 | 0.65 / 0 | 1.5 / 0 | 900 / 900 | 250 / 250 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 5 | Sooo Fast | 1 | 6 | 0.5 | 3.05 / 2.25 | 1500 / 1500 | 0.67 0.62 0 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 6 | Flashfire | 1 | 6 | 0.5 | 2.4 / 2 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 7 | Mega-Duty | 1 | 6 | 1 | 3.2 / 2.2 | 1500 / 1500 | 0.7 / 0 | 1.5 / 0 | 900 / 900 | 250 / 250 | 1.3 / 1.3 | 0.5 0.5 0.5 | 50 |
| 8 | Muscle Tone | 1 | 6 | 0.5 | 3 / 2.2 | 1500 / 1500 | 0.62 / 0 | 1.5 / 0 | 900 / 900 | 250 / 250 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 9 | Sho-Stopper | 1 | 6 | 0.5 | 2.4 / 2 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 10 | Hammered Coupe | 1 | 6 | 0.8 | 2.4 / 2 | 1500 / 1500 | **gc** 0.69 / 0; **pc** 0.62 / 0; **ps2** 0.69 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | **gc** 1.1 / 1.4; **pc** 1.1 / 1.1; **ps2** 1.1 / 1.4 | **gc** 0.7 0.7 0.7; **pc** 0.5 0.5 0.5; **ps2** 0.7 0.7 0.7 | 50 |
| 11 | Jaded | 1 | 6 | 0.5 | 3.1 / 2.5 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 12 | Way 2 Fast | 1 | 6 | 1 | 2.4 / 2 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1.1 / 1 | 0.5 0.5 0.5 | 50 |
| 13 | Deora II | 1 | 6 | 0.5 | 2.7 / 2.1 | 1500 / 1500 | 0.7 / 0 | 1.5 / 0 | 900 / 900 | 250 / 250 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 14 | Hyper Mite | 1 | 7.5 | 0.7 | 4.5 / 3 | 2000 / 2000 | 0.8 / 0 | 1 / 0 | 1200 / 900 | 200 / 150 | 0.8 / 0.8 | 0.5 0.5 0.5 | 50 |
| 15 | Power Pistons | 1 | 6 | 0.35 | 2.3 / 1.9 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 16 | Evil Twin | 1 | 6 | 0.8 | 2.4 / 2 | 1500 / 1500 | 0.7 / 0 | 1.5 / 0 | 900 / 900 | 180 / 180 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 17 | Surf Crate | 1 | 6 | 1 | 2.75 / 2.15 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1.1 / 1 | 0.5 0.5 0.5 | 50 |
| 18 | Rigor Motor | 1 | 6 | 1 | 2.4 / 2 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 19 | Lakester | 1 | 6 | 0.8 | 3 / 2.2 | 1500 / 1500 | 0.62 / 0 | 1.5 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 20 | Splittin' Image 2 | 1 | 6 | 0.5 | 2.7 / 2.1 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 21 | Greased Lightnin' | 1 | 6 | 0.7 | 2.6 / 1.95 | 1500 / 1500 | 0.52 / 0 | 1 / 0 | 1000 / 1000 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 22 | Hyperliner | 1 | 6 | 0.5 | 2.7 / 2.1 | 1500 / 1500 | 0.7 / 0 | 1.5 / 0 | 900 / 900 | 250 / 250 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 23 | Maelstrom | 1 | 6 | 1 | 3 / 3.5 | 2000 / 1500 | 0.75 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 24 | Sol-Aire CX4 | 1 | 6 | 0.35 | 2.3 / 1.95 | 1500 / 1500 | 0.5 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 25 | Surfin' School Bus | 1 | 5 | 1 | 4.7 / 2.2 | 1500 / 1500 | 0.67 / 0 | 1 / 0 | 1250 / 1250 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 26 | Vulture | 1 | 6 | 0.3 | 2.7 / 2.1 | 2000 / 2000 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 250 | 1 / 1 | 0.5 0.5 0.5 | 0 |
| 27 | Arachnorod | 1 | 6 | 0.3 | 2.3 / 1.9 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 1100 / 1100 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 28 | Krazy 8; | 1 | 6 | 0.8 | 2.35 / 1.95 | 1500 / 1500 | 0.62 / 0 | 1.5 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 29 | Shadow Jet | 1 | 6 | 1 | 3 / 2.3 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 30 | HW Prototype 12 | 1 | 6 | 0.4 | 2.3 / 1.9 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 900 / 900 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 31 | Power Rocket | 1 | 6 | 0.5 | 2.4 / 2 | 1500 / 1500 | 0.62 / 0 | 1 / 0 | 1100 / 1100 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |
| 32 | Jet Threat 3.0 | 1 | 6 | 0.3 | 2.5 / 2 | 1500 / 1500 | 0.65 / 0 | 1 / 0 | 1100 / 1100 | 150 / 150 | 1 / 1 | 0.5 0.5 0.5 | 50 |

## ARMOR: health and damage

maxHealth / damageThreshold (CarACollider), bumper damage per closing speed (damageScale), parts breaking off. One value: all of gc/pc/ps2 agree; else per release.

| car | name | maxHealth | damageThreshold | damageScale | maxImpactVel | damagePercent | damageLevel | mass | box (min xyz w, max xyz w) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Twin Mill 2 | 1000 | 0 | 0.05 | 100 | 75 | 20 | 20 | -4 0 -9 0 4 3.5 9 0 |
| 1 | At-A-Tude | 1100 | 0 | 0.05 | 50 | 75 | 20 | 20 | -4.25 -0.5 -10 0 4.25 3.125 11 0 |
| 2 | Overbored 454 | 1000 | 0 | 0.07 | 50 | 75 | 20 | 20 | -4.25 0 -10.5 0 4.25 3 9 0 |
| 3 | MS-T Suzuka | 1000 | 0 | 0.05 | 60 | 75 | 20 | 20 | -4.2 0 -8.5 0 4.2 2.8 7.2 0 |
| 4 | Super Tuned | 1100 | 0 | 0.05 | 50 | 75 | 20 | 20 | -4.5 0 -10 0 4.5 3 9 0 |
| 5 | Sooo Fast | 1000 | 0 | 0.05 | 50 | 75 | 20 | 20 | -5 0 -9 0 5 3.5 9 0 |
| 6 | Flashfire | 1000 | 0 | 0.07 | 50 | 75 | 20 | 20 | -4.5 0 -10.75 0 4.5 2.8 8.5 0 |
| 7 | Mega-Duty | 1300 | 5 | 0.1 | 50 | 75 | 20 | 25 | -4.25 -1.4 -9 0 4.25 3.75 9 0 |
| 8 | Muscle Tone | 1000 | 0 | 0.05 | 50 | 75 | 20 | 20 | -4.5 0 -9.75 0 4.5 3.25 9 0 |
| 9 | Sho-Stopper | 1000 | 0 | 0.06 | 50 | 75 | 20 | 20 | -4 0 -9 0 4 3 9 0 |
| 10 | Hammered Coupe | 1100 | 0 | 0.05 | 50 | 75 | 20 | 20 | -4.6 0 -10 0 4.6 3 9 0 |
| 11 | Jaded | 1000 | 0 | 0.15 | 50 | 75 | 20 | 20 | -4.4 0.5 -9 0 4.4 3.25 9 0 |
| 12 | Way 2 Fast | 1000 | 0 | 0.08 | 50 | 75 | 20 | 20 | -5 0 -12 0 5 4.4 10.5 0 |
| 13 | Deora II | 1600 | 5 | 0.07 | 50 | 75 | 20 | 20 | -4 -0.5 -9 0 4 3.25 9 0 |
| 14 | Hyper Mite | 1000 | 0 | 0.12 | 50 | 75 | 20 | 15 | -3 0 -4.4 0 3 5 3.5 0 |
| 15 | Power Pistons | 1000 | 0 | 0.05 | 50 | 75 | 20 | 20 | -4.25 0 -10.25 0 4.25 2.5 9.25 0 |
| 16 | Evil Twin | 1000 | 0 | 0.1 | 50 | 75 | 20 | 20 | -4.5 0 -12 0 4.5 3 9.5 0 |
| 17 | Surf Crate | 1000 | 0 | 0.05 | 50 | 75 | 20 | 20 | -5 0 -12 0 5 4.4 9.5 0 |
| 18 | Rigor Motor | 1000 | 0 | 0.06 | 50 | 75 | 20 | 20 | -4.74 0 -11.33 0 4.75 3 11.33 0 |
| 19 | Lakester | 1300 | 5 | 0.08 | 50 | 75 | 20 | 20 | -4.2 0 -10 0 4.2 3.5 9 0 |
| 20 | Splittin' Image 2 | 1000 | 0 | 0.05 | 50 | 75 | 20 | 20 | -4 0 -10 0 4 3 9 0 |
| 21 | Greased Lightnin' | 1000 | 0 | 0.09 | 50 | 75 | 20 | 20 | -3.67 -0.5 -10 0 3.67 3 11 0 |
| 22 | Hyperliner | 1500 | 5 | 0.1 | 50 | 75 | 20 | 20 | -4 -1 -9 0 4 4 9 0 |
| 23 | Maelstrom | 1000 | 0 | 0.1 | 50 | 75 | 20 | 20 | -4.5 0 -7.7 0 4.5 2.5 8 0 |
| 24 | Sol-Aire CX4 | 1000 | 0 | 0.05 | 50 | 75 | 20 | 20 | -4.25 0 -10.5 0 4.25 2.5 8.5 0 |
| 25 | Surfin' School Bus | 1700 | 25 | 0.14 | 25 | 75 | 20 | 40 | -4.5 0 -14 0 4.5 5.25 13 0 |
| 26 | Vulture | 1000 | 1 | 0.06 | 50 | 75 | 20 | 20 | -4.5 0 -7 0 4.5 4 8 0 |
| 27 | Arachnorod | 1000 | 5 | 0.06 | 50 | 75 | 20 | 20 | -4.5 0 -13.25 0 4.5 3 10.75 0 |
| 28 | Krazy 8; | 1450 | 0 | 0.09 | 50 | 75 | 20 | 20 | -4.5 0 -7.7 0 4.5 2.5 7 0 |
| 29 | Shadow Jet | 1000 | 0 | 0.04 | 50 | 75 | 20 | 20 | -4 0 -9 0 4 4 9 0 |
| 30 | HW Prototype 12 | 1000 | 10 | 0.09 | 50 | 75 | 20 | 20 | -4.25 0 -11 0 4.25 3.5 11 0 |
| 31 | Power Rocket | 1350 | 5 | 0.06 | 50 | 75 | 20 | 20 | -4.1 0 -11.5 0 4.1 3 11.5 0 |
| 32 | Jet Threat 3.0 | 1000 | 0 | 0.07 | 50 | 75 | 20 | 20 | -4.5 -0.25 -11 0 4.5 2 9 0 |

## STUNT: air control and scoring

rotation rates (rad/s), the longest stunt, points per rotation, turbo per point. One value: all of gc/pc/ps2 agree; else per release.

| car | name | pitchRate | rollRate | spinRate | maxTime | pitchScore | rollScore | spinScore | turboPerPoint | startDelay | minFail | maxFail | airDownforce (base rate max) | spinImpulse | spinDecay |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Twin Mill 2 | 1.9 | 1.9 | 2.5 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 1 | At-A-Tude | 1.95 | 1.95 | 2.55 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 2 | Overbored 454 | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 3 | MS-T Suzuka | 1.9 | 1.9 | 2.5 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 0 2000 | 37 | 1000 |
| 4 | Super Tuned | 1.9 | 1.9 | 2.5 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 5 | Sooo Fast | 1.9 | 1.9 | 2.5 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 6 | Flashfire | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 7 | Mega-Duty | 1.9 | 1.9 | 2.5 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 8 | Muscle Tone | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 9 | Sho-Stopper | 1.9 | 1.9 | 2.5 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 10 | Hammered Coupe | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 11 | Jaded | 2.2 | 2.2 | 2.8 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 12 | Way 2 Fast | 1.9 | 1.9 | 2.5 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 13 | Deora II | 1.9 | 1.9 | 2.5 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 14 | Hyper Mite | 2.1 | 2.1 | 2.7 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 15 | Power Pistons | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 16 | Evil Twin | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 17 | Surf Crate | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 18 | Rigor Motor | 1.95 | 1.95 | 2.55 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 19 | Lakester | 2.1 | 2.1 | 2.7 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 20 | Splittin' Image 2 | 2.2 | 2.2 | 2.8 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 21 | Greased Lightnin' | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 22 | Hyperliner | 2 | 2 | 2.6 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 23 | Maelstrom | 2.3 | 2.3 | 2.9 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 24 | Sol-Aire CX4 | 2.05 | 2.05 | 2.65 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 25 | Surfin' School Bus | 1.8 | 1.8 | 2.4 | 400 | 120 | 100 | 60 | 20 | 100 | 50 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 26 | Vulture | 2.5 | 2.5 | 3.1 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 27 | Arachnorod | 2.1 | 2.1 | 2.7 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 28 | Krazy 8; | 2.05 | 2.05 | 2.65 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 29 | Shadow Jet | 2.4 | 2.4 | 3 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 30 | HW Prototype 12 | 2.2 | 2.2 | 2.8 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 31 | Power Rocket | 2.1 | 2.1 | 2.7 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |
| 32 | Jet Threat 3.0 | 2.6 | 2.6 | 3.2 | 400 | 120 | 100 | 60 | 20 | 100 | 100 | 1100 | 600 1.5 2000 | 37 | 1000 |

## The PS2 prototype's cars

Twin: the release car it is (Port_Proto.c s_cars). Stats differing from the twin's (on the first release read) in bold; the physics groups list only the fields that differ from the twin.

| proto car | name | twin | SPEED [0] | ARMOR [1] | GRIP [2] | STUNT [3] | physics fields not as the twin |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | Twin Mill 2 | 0 Twin Mill 2 | 44 | 30 | 30 | 30 | damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 1 | Fandango | - | 30 | 50 | 45 | 30 | (no twin) |
| 2 | Sooo Fast | 5 Sooo Fast | 36 | 30 | 65 | 30 | damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 3 | MS-T Suzuka | 3 MS-T Suzuka | 44 | 30 | 40 | 30 | damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4.5 0 -7.7 0 4.5 2.5 8 0 (twin -4.2 0 -8.5 0 4.2 2.8 7.2 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 4 | Super Tuned | 4 Super Tuned | 30 | 40 | 50 | 30 | tractionLift 0.5 (twin 0.35); damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4.5 0 -10 0 4.5 2.5 9 0 (twin -4.5 0 -10 0 4.5 3 9 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 5 | Surf Crate | 17 Surf Crate | 36 | 30 | 55 | 40 | damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 6 | Sho-Stopper | 9 Sho-Stopper | 50 | 30 | 40 | 30 | damageScale 0.02 (twin 0.06); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 7 | At-a-Tude | 1 At-A-Tude | 35 | 40 | 50 | 35 | damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4.4 -0.5 -11.25 0 4.4 3.125 10 0 (twin -4.25 -0.5 -10 0 4.25 3.125 11 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 8 | OverBoard | 2 Overbored 454 | 44 | 30 | 55 | 40 | damageScale 0.02 (twin 0.07); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 9 | Flashfire | 6 Flashfire | 46 | 30 | 40 | 40 | damageScale 0.02 (twin 0.07); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 10 | Mega Duty | 7 Mega-Duty | 30 | 60 | 60 | 30 | damageScale 0.02 (twin 0.1); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 11 | Muscle Tone | 8 Muscle Tone | 42 | 30 | 60 | 40 | damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 12 | Sweet 16 II | - | 52 | 30 | 30 | 35 | (no twin) |
| 13 | Hammered Coupe | 10 Hammered Coupe | 38 | 40 | 40 | 40 | tractionLift 0.5 (twin 0.8); handbrakeGrip 0.62 / 0 (twin 0.69 / 0); restLength 1 / 1 (twin 1.1 / 1.4); inertiaScale 0.5 0.5 0.5 (twin 0.7 0.7 0.7); damageScale 0.01 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -5.125 0 -12.5 0 5.125 3.75 10.5 0 (twin -4.6 0 -10 0 4.6 3 9 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 14 | Hypermite | 14 Hyper Mite | 45 | 30 | 100 | 50 | tractionLift 1 (twin 0.7); friction 3.8 / 3 (twin 4.5 / 3); handbrakeGrip 0.9 / 0 (twin 0.8 / 0); springRate 1000 900 1000 1000 (twin 1200 / 900); damping 150 / 150 (twin 200 / 150); damageScale 0.02 (twin 0.12); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -3 -1 -4.4 0 3 5 3.5 0 (twin -3 0 -4.4 0 3 5 3.5 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 15 | Power Pistons | 15 Power Pistons | 62 | 30 | 25 | 40 | tractionLift 0.5 (twin 0.35); damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 16 | Deora 2 | 13 Deora II | 30 | 90 | 50 | 30 | damageScale 0.02 (twin 0.07); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 2.5 (twin 120); rollScore 2 (twin 100); spinScore 1.5 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 17 | Jester | - | 51 | 30 | 50 | 60 | (no twin) |
| 18 | Way 2 Fast | 12 Way 2 Fast | 60 | 30 | 35 | 30 | damageScale 0.02 (twin 0.08); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 19 | Evil Twin | 16 Evil Twin | 44 | 50 | 40 | 30 | damageScale 0.02 (twin 0.1); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4.5 0 -12 0 4.5 2.25 9.5 0 (twin -4.5 0 -12 0 4.5 3 9.5 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 20 | Jaded | 11 Jaded | 62 | 30 | 50 | 60 | friction 2.7 / 2.1 (twin 3.1 / 2.5); damageScale 0.02 (twin 0.15); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4.4 -1 -9.5 0 4.4 3.25 9.5 0 (twin -4.4 0.5 -9 0 4.4 3.25 9 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 21 | Rigor Mortor | 18 Rigor Motor | 68 | 30 | 40 | 35 | damageScale 0.02 (twin 0.06); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 22 | Lakester | 19 Lakester | 50 | 60 | 60 | 50 | damageScale 0.02 (twin 0.08); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100); spinImpulse 25 (twin 37) |
| 23 | Splitin' Image 2 | 20 Splittin' Image 2 | 62 | 30 | 50 | 60 | damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 24 | Greased Lightnin' | 21 Greased Lightnin' | 70 | 30 | 30 | 40 | friction 2.35 / 1.95 (twin 2.6 / 1.95); handbrakeGrip 0.62 / 0 (twin 0.52 / 0); damageScale 0.02 (twin 0.09); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -5 0.5 -12.5 0 5 3.75 11.75 0 (twin -3.67 -0.5 -10 0 3.67 3 11 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 25 | Hyperliner | 22 Hyperliner | 36 | 80 | 50 | 40 | damageScale 0.02 (twin 0.1); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 26 | Maelstrom | 23 Maelstrom | 58 | 30 | 60 | 70 | friction 3 / 2.2 (twin 3 / 3.5); maxFriction 1500 / 1500 (twin 2000 / 1500); handbrakeGrip 0.62 / 0 (twin 0.75 / 0); driveBlend 1.5 / 0 (twin 1 / 0); damping 250 / 250 (twin 150 / 150); damageScale 0.02 (twin 0.1); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 27 | Sol-aire CX-4 | 24 Sol-Aire CX4 | 76 | 30 | 30 | 45 | tractionLift 0.5 (twin 0.35); handbrakeGrip 0.62 / 0 (twin 0.5 / 0); damageScale 0.02 (twin 0.05); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 28 | Surfin School Bus | 25 Surfin' School Bus | 44 | 100 | 40 | 25 | damageThreshold 10 (twin 25); damageScale 0.04 (twin 0.14); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4.5 0 -13 0 4.5 5.25 13 0 (twin -4.5 0 -14 0 4.5 5.25 13 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 50); maxFail 650 (twin 1100) |
| 29 | Locked | 29 Shadow Jet | 66 | 30 | 60 | 80 | damageScale 0.02 (twin 0.04); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4 0 -9 0 4 3 9 0 (twin -4 0 -9 0 4 4 9 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 30 | AracnoRod | 27 Arachnorod | 92 | 30 | 25 | 50 | tractionLift 0.5 (twin 0.3); damageScale 0.02 (twin 0.06); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 31 | Krazy 8s | 28 Krazy 8; | 68 | 75 | 30 | 45 | damageScale 0.02 (twin 0.09); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 32 | Vulture | 26 Vulture | 80 | 30 | 50 | 90 | damageScale 0.02 (twin 0.06); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4.5 0 -7 0 4.5 3 8 0 (twin -4.5 0 -7 0 4.5 4 8 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 33 | HW Prototype 12 | 30 HW Prototype 12 | 100 | 30 | 25 | 60 | tractionLift 0.5 (twin 0.4); damageScale 0.02 (twin 0.09); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 34 | Power Rocket | 31 Power Rocket | 82 | 65 | 40 | 50 | damageScale 0.02 (twin 0.06); damagePercent 99 (twin 75); damageLevel 1 (twin 20); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
| 35 | Jet Threat 3 | 32 Jet Threat 3.0 | 90 | 30 | 35 | 100 | tractionLift 1 (twin 0.3); damageScale 0.02 (twin 0.07); damagePercent 99 (twin 75); damageLevel 1 (twin 20); box (min xyz w, max xyz w) -4 0 -9 0 4 3 7.5 0 (twin -4.5 -0.25 -11 0 4.5 2 9 0); pitchScore 75 (twin 120); rollScore 60 (twin 100); spinScore 35 (twin 60); minFail 10 (twin 100); maxFail 650 (twin 1100) |
