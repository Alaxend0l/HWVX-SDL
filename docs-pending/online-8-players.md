# Online: 8 players, and mods (for HWVX-SDL/README.md "Online play", or docs/xdl-format/README.md §10.5 next to "online choices by id")

Sessions take up to 8 players (NET_PROTOCOL 3). The retail game has 4 player ports; online:

- Ports 5-8 are human players from extra pool slots (PlayerManager.c), with their car, item and
  round results kept past the code variables' 4 (GameSettings.c, online only).
- Spawns: Battle worlds have 4 player spawns and 3 AI spawns; players 5-7 use the AI spawns
  (AI is off online), player 8 a spot of ours. Drag Race worlds have only the 4: players 5-8
  start a row behind them (a spot found from the world's collision, the same on every machine).
  Joyride's single spawn gets 7 spots around it as before. Offline nothing changes.
- Scripts: a trigger or action whose car list names all of the world's 4 player spawns
  (airboxes, control on/off, teleports) includes players 5-8. A Drag Race's per-player finish
  counts for players 5-8 at player 1's finish volume; the host decides the winner as for
  1-4, and player 1's finish actions run for them (camera on the winner), without the
  "Player 1 wins" screen.
- The round summary has four rows; with players 5-8 in the game it shows P1-P4 and P5-P8 in
  turns (3 s each). No auto-advance online in Battle and Drag Race (unchanged).
- Mods: each machine sends a fingerprint of its enabled mods (FNV-1a of "id@version" in load
  order, plus names) in MSG_HELLO on joining. A client whose mods differ from the host's
  leaves with "Mods differ. Host: ... Here: ..."; the host leaves it out of games and shows
  "Player N has other mods" in the lobby. No mods on both sides: they match.
- A registered track goes in MSG_SETUP by id, with the level of the track it is made from
  for a machine without it (levels past a script's own are numbered by each ids.lock).
