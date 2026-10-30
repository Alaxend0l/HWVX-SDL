#!/bin/sh
# Regenerate src/game/data_gen.c (see tools/gen_data.py). The name list is the
# globals the game's C uses but doesn't define; add names when the link fails.
#   --raw:     objects whose "pointers" are really image data
#   --hostptr: objects the C code declares as host pointers (T *name)
#   --bytes:   byte tables (u8 fields): keep the original byte order
cd "$(dirname "$0")/.." || exit 1
exec python3 tools/gen_data.py --raw rawData \
    --hostptr g_pPlayerManager --hostptr g_rndNumber \
    --hostptr pTriggerFuncArray --hostptr pActionFuncArray \
    --bytes g_keyBinding \
    @tools/gen_data_names.txt
