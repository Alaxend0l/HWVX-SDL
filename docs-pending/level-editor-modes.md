# Level editor: the modes (task 21.5), for docs/xdl-format §10.6 and the level editor guide

## The modes, as the game makes them

A level's `mode` picks its progression (challenge 1, mission, battle, drag, joyride: `MODES` in
tools/xdl_level.py) and `extends` the retail level of that mode (`base://mcp/track/N`): its loading
screen, steps, and win and lose sequences. A level ends through its triggers:

- **Challenge, Mission, Joyride:** an `end_level` action, `{"result": 4, "arg": 0}` to win, any
  other result to lose. The game logs "progression: the level ends: won/lost".
- **Battle, Drag:** rounds (GameRules). The level's `rules` block (`mode`: points, kills or none;
  the points or kills to win, rounds, time) becomes a generated `_rules` level_start trigger that
  sets the GameRules code variables 0x12C–0x135. A `round_complete {"player": N}` action ends a
  round for player N (GameRules_ForceRoundEnd). Logs: "rules: the round ends, player N wins it" and
  "rules: all rounds complete, player 1 won/lost".

tools/level_summary.py WORLD lists a retail level's records and triggers, one line each, so you
can see how a mode's levels are made. For example, race1l2's start: trigger 19 locks the four cars
and sets variable 6; trigger 26 then unlocks them 4.5 s later and sets variable 6 back to 0 at once.

## The mode checks (compile and check warnings; the game still runs the level)

- **Drag:** exactly 4 starts, one per lane.
- **Battle:** at least 2 starts.
- **Every mode:** a player start (port 1) is required (an error). `rules` outside battle and
  drag is warned.
- **AI is used only the way retail uses it in that mode and world.** The checks warn when an AI
  car is put in Joyride (retail has none), in a world whose retail level has none, or as a
  level's own AI car off the retail routes.
- **Lane check** (the browser's Rules editor, viewer command `lanecheck`): each drag start's
  straight path to the finish, as wide as the spawn box (Port_SpawnEnvelope). It reports what
  blocks a lane, such as a gantry leg.
- **Trigger limits:** the number of triggers of each type, the level's plus the kept base ones,
  must stay within TriggerStats[type].max (`TRIGGER_MAX`).

## Own variables' ids

Interfaces bind world variables by `id`; on a match they write the screen's variable by the world
variable's index. A level's own variable with id 0 can therefore be bound by a retail screen.
That stalled the drag player in testing. Own variables without an `id` get ids from 249 down,
which no retail screen uses.

## New level per mode

The browser's New level dialog takes a mode and a retail world of that mode. It writes a project
that extends that mode's retail level, with `baseTriggers: "all"`.

## The samples (tools/level-samples; links to the player's data only, no game data)

| sample | mode | world | won by | lost by |
|---|---|---|---|---|
| checkpoint-run | challenge | ch1_l2 (21.4; tools/level_test.sh) | the checkpoints | the clock |
| gear-grab | joyride | world1 | both own gears within 30 s | the clock |
| two-gates | mission | world1l1 | gate 1, then gate 2 (objectives) within 35 s | the clock |
| flag-run | battle | bat1_l1 | the flag volume (round_complete 1) | the time-out (round_complete 2) |
| short-sprint | drag | race1l2 | the finish sphere first (fin1–4: round_complete per car) | an opponent first |

short-sprint keeps the retail AI and its roles: three opponents on the retail grid. A `handicap`
trigger (level_start) uses the AI's own retail controls to hold them 2.5 s longer, locked at
+4.6 s and unlocked at +7 s.

tools/level_modes_test.sh BUILD DATA OUT [SAMPLE...] builds each sample into its world pack the
way the test drive does. It then plays each sample twice with scripted input, the same on every
run: "win" must end the level won (or player 1 wins the round) and "lose" must end it lost.

A sample whose level.jsonc has `"smoke": {"route": "x,z;..."}` (two-gates, short-sprint) also
gets a smoke run, with HWVX_AUTOPILOT driving along that route. **This is a smoke test only:** it
checks that the level loads, starts, and runs without a crash or soft-lock in what the autopilot
reaches. It is not a measure of whether a level can be won, or of how hard it is. The game's AI
isn't good enough for that, and the editor never presents it as such. The judge of beatability
is a person's Test drive (F5); the editor's own validation stays structural (links resolve,
triggers can fire, objectives chain, starts clear, limits).
