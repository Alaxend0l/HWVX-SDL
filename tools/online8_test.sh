#!/bin/bash
# tools/online8_test.sh BUILD DATADIR OUTDIR MODE [SECONDS] [PLAYERS] [WORLD]
#
# An online session of PLAYERS (default 8) on this machine: one game hosting
# (HWVX_ONLINE_TEST, see Port_Online.c) and PLAYERS-1 tools/netbot.c clients,
# in MODE (battle, drag, joyride) on WORLD (default 0), for SECONDS (default
# 90). Every 60th frame lands in OUTDIR/dump-MODE, the logs in OUTDIR. The
# game is force-killed at the end. Exit status 0 if it passes:
#   - the game ran the whole time (no crash) and started the level with
#     PLAYERS human cars, each at its own spawn;
#   - every bot was in the session to the end, without errors (the scores the
#     host sent for it are what it reported), went through every go, heard of
#     every other car, and (battle, drag) saw the rounds end;
#   - the host's copy of each bot's car stayed near where the bot said
#     (online: sync, off by less than SYNC_MAX units, default 12);
#   - drag: the round a bot on port 5-8 claimed went to it;
#   - every car settled where it spawned: not pushed off (MOVED), not fallen (FELL), no wall
#     inside its footprint (WALL), no two footprints overlapping (OVERLAP).
# Drag Race takes 4 players at most: with more, the test passes if the host refuses.
# CAR=N puts every player in car N (25: Surfin' School Bus, the largest).
# GAP=P leaves port P (2-8) empty: PLAYERS+1 join, port P's bot leaves in the
# lobby, and the others keep their ports (players past 4 then start at P's spawn
# when P <= 4); every remaining port must still drive and be seen by the host.
# Needs the bots built: cmake -DHWVX_TESTS=ON, target netbot. One game only.
B="$1"; D="$2"; O="$3"; MODE="$4"; T="${5:-90}"; N="${6:-8}"; W="${7:-0}"
[ -z "$MODE" ] && { sed -n 2,20p "$0"; exit 2; }
SYNC_MAX="${SYNC_MAX:-12}"
mkdir -p "$O"; O=$(cd "$O"; pwd)
TAG="$MODE$W-$N${GAP:+-gap$GAP}"
rm -rf "$O/dump-$TAG" "$O/pref-$TAG" "$O/$TAG".*.log; mkdir -p "$O/dump-$TAG" "$O/pref-$TAG"
PORT=47815

LAST=$N; [ -n "$GAP" ] && LAST=$((N + 1))
PORTS=$(seq 2 "$LAST" | grep -vx "${GAP:-0}")
FINISH_PORT=7
[ "$LAST" -lt 7 ] && FINISH_PORT=$LAST
[ "$FINISH_PORT" = "$GAP" ] && FINISH_PORT=$((GAP - 1))
env HWVX_NO_MOVIE=1 HWVX_NO_DIALOG=1 HWVX_SEED=3 HWVX_MODS=0 HWVX_AUTOINPUT="700:start,+400:start" \
    HWVX_ONLINE_TEST="host,players=$N,mode=$MODE,world=$W,rounds=2,time=25,summary=8${CAR:+,car=$CAR}${GAP:+,lobby=8}" \
    HWVX_PREF_DIR="$O/pref-$TAG" HWVX_DUMP="$O/dump-$TAG" \
    "$B/hwvx" --windowed --window 1280x960 --data "$D" > "$O/$TAG.host.log" 2>&1 &
G=$!
# whatever happens, no game or bot outlives the test
trap 'kill -9 $G "${BOTS[@]}" 2>/dev/null' EXIT
# the host is up once its lobby is
for i in $(seq 60); do grep -q 'online: hosting' "$O/$TAG.host.log" 2>/dev/null && break; sleep 1; done
BOTS=()
for p in $(seq 2 "$LAST"); do
    extra=""
    [ "$p" = "$GAP" ] && extra="--seconds 3"
    [ "$MODE" = drag ] && [ "$p" = "$FINISH_PORT" ] && extra="--finish 8"
    "$B/netbot" "127.0.0.1:$PORT" --drive --points 10 --seconds $((T - 5)) ${CAR:+--vehicle $CAR} $extra > "$O/$TAG.bot$p.log" 2>&1 &
    BOTS+=($!)
    sleep 0.3
done
for i in $(seq "$T"); do sleep 1; kill -0 $G 2>/dev/null || break; done
ALIVE=0; kill -0 $G 2>/dev/null && ALIVE=1
for b in "${BOTS[@]}"; do wait $b; done
kill -9 $G 2>/dev/null; wait $G 2>/dev/null

fail=0
bad() { echo "FAIL: $*"; fail=1; }
H="$O/$TAG.host.log"
[ $ALIVE = 1 ] || bad "the game exited early (crash?): $(tail -3 "$H")"
if [ "$MODE" = drag ] && [ "$N" -gt 4 ]; then
    # Drag Race takes 4 at most: the host must refuse, and nobody start a level
    grep -q 'Drag Race: .* 4 at most' "$H" && grep -q 'refused to start' "$H" || bad "drag with $N players wasn't refused"
    grep -q 'level ready' "$H" && bad "drag with $N players started a level"
    grep -l 'level starting' "$O/$TAG".bot*.log >/dev/null 2>&1 && bad "a bot started a drag level with $N players"
    [ $fail = 0 ] && echo "PASS: drag with $N players refused" || echo "see $O/$TAG.*.log"
    exit $fail
fi
grep -q "level ready: .*$N human cars" "$H" || bad "no level with $N human cars: $(grep 'level ready' "$H" | head -1)"
grep -q 'online: no clear spawn' "$H" && echo "note: $(grep -c 'online: no clear spawn' "$H") spawn(s) without a clear spot"
want=0; for p in 1 $PORTS; do want=$((want | (1 << (p - 1)))); done
want=$(printf '%02X' $want)
for p in $PORTS; do
    L="$O/$TAG.bot$p.log"
    S=$(grep 'summary:' "$L")
    [ -n "$S" ] || bad "bot $p: no summary: $(tail -2 "$L")"
    grep -q 'ERROR' "$L" && bad "bot $p: $(grep ERROR "$L" | head -2)"
    gos=$(echo "$S" | sed -n 's/.*gos \([0-9]*\).*/\1/p')
    [ "${gos:-0}" -ge 1 ] || bad "bot $p: never got go"
    if [ "$MODE" != joyride ]; then
        ends=$(echo "$S" | sed -n 's/.*round ends \([0-9]*\).*/\1/p')
        [ "${ends:-0}" -ge 1 ] || bad "bot $p: saw no round end"
    fi
    heard=$(echo "$S" | sed -n 's/.*cars heard of \([0-9A-F]*\).*/\1/p')
    [ "$heard" = "$want" ] || bad "bot $p: heard of cars $heard, not $want"
done
# the host's copies against what their owners said: the worst after the first sync of a stage
worst=$(grep 'online: sync' "$H" | sed -n 's/.*off by:\(.*\)/\1/p' | tr ' ' '\n' | sed -n 's/^[0-9]*://p' | sort -n | tail -1)
echo "sync: $(grep -c 'online: sync' "$H") checks, worst ${worst:-none}"
# every other player's car is seen at the host: in its sync lines
for p in $PORTS; do
    grep 'online: sync' "$H" | grep -q " $p:[0-9]" || bad "port $p: its car never synced at the host (no car, or not its port's)"
done
[ -n "$GAP" ] && grep 'online: sync' "$H" | grep -q " $GAP:[0-9]" && bad "port $GAP left but has a car"
if [ -n "$worst" ] && awk "BEGIN{exit !($worst > $SYNC_MAX)}"; then
    # one bad sample right as a stage starts is the snap; more is a desync
    over=$(grep 'online: sync' "$H" | sed -n 's/.*off by:\(.*\)/\1/p' | tr ' ' '\n' | sed -n 's/^[0-9]*://p' | awk "\$1 > $SYNC_MAX" | wc -l)
    [ "$over" -gt 2 ] && bad "host's copies off by up to $worst units ($over samples over $SYNC_MAX)"
fi
grep 'online test: car 1,' "$H" | head -1 | sed 's/.*online test: /spawns: /'
bad_spawn=$(grep -E 'online test: .*(MOVED|FELL|WALL|OVERLAP)' "$H" | sed 's/.*online test: //' | sort | uniq -c | head -8)
[ -n "$bad_spawn" ] && bad "spawns: $(echo $bad_spawn)"
if [ "$MODE" = drag ]; then
    # a player at another port's spawn races in its lane: the script renumbers by spawn
    # (ActionSetPlayerOrder), so the round goes to that lane's number
    LANE=$(sed -n "s/.*online: port $FINISH_PORT starts at the world's spawn [0-9]* (port \([0-9]*\)'s).*/\1/p" "$H" | head -1)
    LANE=${LANE:-$FINISH_PORT}
    grep -q "round won by $LANE" "$H" || bad "drag: port $FINISH_PORT's claimed finish (lane $LANE) didn't win: $(grep 'round won' "$H" | head -2)"
fi
echo "frames: $(ls "$O/dump-$TAG" | wc -l | tr -d ' ') in $O/dump-$TAG"
[ $fail = 0 ] && echo "PASS: $MODE world $W, $N players" || echo "see $O/$TAG.*.log"
exit $fail
