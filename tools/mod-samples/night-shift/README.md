# Night Shift (sample mod)

`mod.jsonc` is the whole mod: a car that extends `base://HW_Cars/car_5` with a few changed
fields and a recolour recipe, plus a patch and a tint on base car 7. No game data.

    python3 tools/xdl_mod.py index <your game files> -o build/mod-index.npz
    python3 tools/xdl_mod.py pack tools/mod-samples/night-shift -o build/night-shift.hwmod --index build/mod-index.npz
    python3 tools/xdl_mod.py install build/night-shift.hwmod "<preferences>/mods"

or run the port with `HWVX_MODS_DIR=tools/mod-samples`.
