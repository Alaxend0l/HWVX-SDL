#!/usr/bin/env python3
"""Draft layouts of the TAC parameter lists (research; see tac_survey.py)."""

import struct

WORD_KINDS = ("u32", "s32", "f32", "tok", "word")

# trigger kinds: (name, layout)
TRIGGERS = {
    0: ("true", []),
    1: ("airbox_entered", [("list", "volumes", "WorldVolume"), ("list", "cars", "TileCar")]),
    2: ("airbox_exited", [("list", "volumes", "WorldVolume"), ("list", "cars", "TileCar")]),
    3: ("airbox_stop_in", [("list", "volumes", "WorldVolume"), ("list", "cars", "TileCar")]),
    4: ("destructible_killed", [("list", "instances", "QuadInstance"), ("u32", "how")]),
    5: ("creature_killed", [("list", "creatures", "TileCreature")]),
    6: ("item_pickup", [("list", "items", "QuadItem"), ("list", "cars", "TileCar")]),
    7: ("interface_end", [("link", "interface", None)]),
    8: ("level_start", []),
    9: ("timer_value_check", [("list", "timers", "WorldTimerSlot"), ("operand", "value"), ("tok", "op")]),
    10: ("moveable_end", [("list", "paths", "TilePath")]),
    11: ("variable_value_check", [("list", "variables", "WorldVariableSlot"), ("operand", "value"), ("tok", "op")]),
    12: ("vehicle_killed", [("list", "cars", "TileCar")]),
    13: ("p2p_activated", [("rest", "words")]),
    14: ("creature_dropped_off", [("list", "creatures", "TileCreature"), ("list", "cars", "TileCar")]),
    15: ("interface_pack_end", [("u32", "id"), ("str", "pack")]),
    16: ("vehicle_damage", [("list", "cars", "TileCar"), ("tok", "test"), ("s32", "amount"), ("scratch", "saved", "cars")]),
    17: ("player_vehicle_gear", [("u32", "unused"), ("u32", "gear")]),
    18: ("player_vehicle_in_air", [("u32", "unused"), ("s32", "time"), ("u32", "inAir")]),
    19: ("item_volume_entered", [("list", "volumes", "WorldVolume"), ("list", "items", "QuadItem")]),
    20: ("code_variable_value_check", [("tok", "op"), ("codevar", "var"), ("operand", "value")]),
    21: ("creature_picked_up", [("rest", "words")]),
    22: ("game_rounds_complete", []),
    23: ("game_round_x_complete", []),
    24: ("item_dropped", [("list", "items", "QuadItem"), ("list", "cars", "TileCar")]),
    25: ("movable_volume_entered", [("list", "volumes", "WorldVolume"), ("list", "paths", "TilePath")]),
    26: ("sound_seq_queue_end", []),
    27: ("creature_volume_entered", [("list", "volumes", "WorldVolume"), ("list", "creatures", "TileCreature")]),
}

ACTIONS = {
    0: ("airbox_change_mode", [("tok", "mode"), ("list", "volumes", "WorldVolume")]),
    1: ("camera_set_targets", [("u32", "player"), ("rest", "camera")]),
    2: ("destructible_change_mode", [("tok", "mode"), ("list", "instances", "QuadInstance")]),
    3: ("fx_point_change_mode", [("tok", "mode"), ("list", "points", "QuadFxPoint")]),
    4: ("interface_play", [("link", "interface", None)]),
    5: ("item_change_mode", [("tok", "mode"), ("list", "items", "QuadItem")]),
    6: ("end_level", [("s32", "result"), ("s32", "arg")]),
    7: ("timer_change_value", [("tok", "op"), ("list", "timers", "WorldTimerSlot"), ("operand", "value")]),
    8: ("timer_change_mode", [("tok", "mode"), ("u32", "type"), ("list", "timers", "WorldTimerSlot")]),
    9: ("moveable_change_mode", [("tok", "speedMode"), ("tok", "posMode"), ("u32", "speed"), ("u32", "unk"), ("list", "paths", "TilePath")]),
    10: ("music_change", [("rest", "words")]),
    11: ("music_set_song", [("rest", "words")]),
    12: ("nav_point_change_mode", [("u32", "enabled"), ("list", "points", "NavPoint")]),
    13: ("pause_game", [("tok", "mode")]),
    14: ("speaker_change_mode", [("tok", "mode"), ("list", "speakers", "TileSpeaker")]),
    15: ("target_arrow_set_target", [("u32", "type"), ("link", "target", None)]),
    16: ("variable_change_value", [("tok", "op"), ("list", "variables", "WorldVariableSlot"), ("operand", "value")]),
    17: ("vehicle_change_mode", [("tok", "mode"), ("list", "cars", "TileCar")]),
    18: ("vehicle_ai_set_named_path", [("rest", "words")]),
    19: ("vertex_color_seq_change_mode", [("tok", "mode"), ("list", "sequences", None)]),
    20: ("print_debug_string", [("u32", "what"), ("u32", "countdown"), ("word", "value")]),
    21: ("vehicle_teleport", [("u32", "unused"), ("link", "teleporter", "Teleporter"), ("list", "cars", "TileCar")]),
    22: ("vehicle_change_control", [("tok", "mode"), ("u32", "unused"), ("list", "cars", "TileCar")]),
    23: ("set_player_vehicle", [("rest", "words")]),
    24: ("vehicle_remove_item", [("u32", "what"), ("list", "cars", "TileCar")]),
    25: ("vehicle_remove_creature", [("rest", "words")]),
    26: ("variable_change_mode", [("tok", "mode"), ("list", "variables", "WorldVariableSlot")]),
    27: ("load_interface_pack", [("u32", "id"), ("str", "pack")]),
    28: ("play_interface_pack", [("u32", "id"), ("str", "pack")]),
    29: ("unload_interface_pack", [("u32", "id"), ("str", "pack")]),
    30: ("end_interface_pack", [("u32", "id"), ("str", "pack")]),
    31: ("end_interface", [("s32", "all"), ("link", "interface", None), ("word", "later")]),
    32: ("hud_change_mode", [("u32", "unused"), ("u32", "on")]),
    33: ("script_light_change_mode", [("rest", "words")]),
    34: ("code_variable_modify_value", [("tok", "op"), ("codevar", "var"), ("operand", "value")]),
    35: ("mission_obj_change_mode", [("tok", "mode"), ("list", "objectives", "WorldObjective")]),
    36: ("pause_tac_system", [("tok", "mode")]),
    37: ("swap_ai_script", [("word", "easy"), ("word", "medium"), ("word", "hard"), ("word", "expert"), ("list", "cars", "TileCar")]),
    38: ("reset_game_round", [("u32", "resetMotion"), ("u32", "resetPosition"), ("u32", "jettison")]),
    39: ("round_complete", [("u32", "player")]),
    40: ("vehicle_give_item", [("u32", "what"), ("word", "item"), ("list", "cars", "TileCar")]),
    41: ("set_player_order", [("link", "p1", "TileCar"), ("link", "p2", "TileCar"), ("link", "p3", "TileCar"), ("link", "p4", "TileCar")]),
    42: ("play_sound_sequence", [("u32", "play"), ("u32", "queued"), ("word", "sequence")]),
    43: ("creature_change_mode", [("tok", "mode"), ("list", "creatures", "TileCreature")]),
}

TRIGGER_NAMES = {k: v[0] for k, v in TRIGGERS.items()}
ACTION_NAMES = {k: v[0] for k, v in ACTIONS.items()}


class LayoutError(Exception):
    pass


def world_regions(pk):
    """{start: (kind, stride, count)} of the world's record arrays."""
    out = {}
    if len(pk.blob) < 0xA0:
        return out

    def arr(cnt_off, ptr_off, stride, kind, base=0):
        t = pk.ptr.get(base + ptr_off)
        if t is None:
            return None
        n = pk.u32(base + cnt_off)
        out[t] = (kind, stride, n)
        return t, n
    arr(0x08, 0x0C, 0x60, "TileCar")
    arr(0x10, 0x14, 0x20, "TilePath")
    q = arr(0x20, 0x24, 0x50, "WorldQuad")
    if q:
        t, n = q
        for i in range(min(n, 0x1000)):
            c = pk.ptr.get(t + i * 0x50 + 0x30)
            if c is None:
                continue
            arr(0x04, 0x08, 0x40, "QuadInstance", c)
            arr(0x0C, 0x10, 0x40, "QuadFxPoint", c)
            arr(0x14, 0x18, 0x40, "QuadItem", c)
    arr(0x28, 0x2C, 0x40, "TileSpeaker")
    arr(0x30, 0x34, 0x70, "Teleporter")
    arr(0x38, 0x3C, 0x20, "TileLight")
    arr(0x40, 0x44, 0x40, "TileCreature")
    arr(0x48, 0x4C, 4, "WorldVariableSlot")
    arr(0x50, 0x54, 4, "WorldTimerSlot")
    arr(0x58, 0x5C, 8, "WorldObjective")
    arr(0x68, 0x6C, 0x30, "WorldVolume")
    nav = pk.ptr.get(0x78)
    if nav is not None:
        arr(0x04, 0x00, 0x60, "NavPoint", nav)
    pk._rstarts = sorted(out)
    return out


def region_of(pk, t):
    import bisect
    i = bisect.bisect_right(pk._rstarts, t) - 1
    if i < 0:
        return None
    s = pk._rstarts[i]
    kind, stride, n = pk.regions[s]
    d = t - s
    if d < stride * n and d % stride == 0:
        return kind, d // stride
    return None


def decode(pk, off, layout, extent):
    """(value, length, scalars) of a parameter list at off."""
    o = off
    val = {}
    scal = {}
    end = off + extent

    def word(name, kind):
        nonlocal o
        if o + 4 > end:
            raise LayoutError(f"{name}: past the node end")
        w = pk.u32(o)
        if o in pk.ptr:
            if kind != "word":
                raise LayoutError(f"{name}: a {kind} on a pointer slot")
            v = ("link", pk.ptr[o])
        else:
            v = w
            if kind == "f32":
                v = struct.unpack(pk.e + "f", struct.pack(pk.e + "I", w))[0]
        o += 4
        return v

    def link(name, target):
        nonlocal o
        if o + 4 > end:
            raise LayoutError(f"{name}: past the node end")
        w = pk.u32(o)
        if o not in pk.ptr:
            if w != 0:
                raise LayoutError(f"{name}: a link slot holding 0x{w:X} without relocation")
            o += 4
            return None
        t = pk.ptr[o]
        o += 4
        r = region_of(pk, t)
        if target and (r is None or r[0] != target):
            raise LayoutError(f"{name}: link to {r} not a {target}")
        return ("link", r if r else t)

    for it in layout:
        k, name = it[0], it[1]
        if k in WORD_KINDS:
            v = word(name, k)
            val[name] = v
            if k in ("tok", "u32", "s32") and not isinstance(v, tuple):
                scal[name] = v
        elif k == "link":
            val[name] = link(name, it[2])
        elif k == "list":
            n = word(name + ".count", "u32")
            if n > 0x400:
                raise LayoutError(f"{name}: count {n}")
            val[name] = [link(f"{name}[{i}]", it[2]) for i in range(n)]
        elif k == "scratch":
            n = len(val[it[2]])
            val[name] = [word(f"{name}[{i}]", "u32") for i in range(n)]
        elif k == "operand":
            t = word(name + ".type", "u32")
            scal[name + ".type"] = t
            if t == 0:
                val[name] = {"const": word(name, "f32")}
            elif t == 1:
                val[name] = {"variable": link(name, "WorldVariableSlot")}
            elif t == 2:
                var = word(name + ".var", "u32")
                val[name] = {"code": var, "arg": word(name + ".arg", "word")}
            else:
                raise LayoutError(f"{name}: operand type {t}")
        elif k == "codevar":
            var = word(name, "u32")
            scal[name] = var
            if var < 100:
                n = word(name + ".count", "u32")
                if n > 0x400:
                    raise LayoutError(f"{name}: count {n}")
                val[name] = {"var": var, "cars": [link(f"cars[{i}]", "TileCar") for i in range(n)]}
            else:
                val[name] = {"var": var, "arg": word(name + ".arg", "word")}
        elif k == "str":
            b = pk.blob[o:end]
            z = b.find(b"\0")
            if z < 0:
                raise LayoutError(f"{name}: no NUL")
            val[name] = b[:z].decode("latin-1")
            o += (z + 4) & ~3
        elif k == "rest":
            ws = []
            while o + 4 <= end:
                ws.append(word(name, "word"))
            val[name] = ws
        else:
            raise ValueError(k)
    return val, o - off, scal


def walk_triggers(pk):
    """Yield a record per trigger, condition and action of a world pack."""
    h = pk.src.header
    n = h.get("num_triggers") or 0
    base = h.get("trigger_offset") or 0
    for i in range(n):
        yield from trigger(pk, base + i * 0x2C, "trigger", 0)


def param_rec(pk, what, kind, slot, layout):
    rec = {"what": what, "kind": kind, "off": slot}
    t = pk.ptr.get(slot)
    if t is None:
        if pk.u32(slot) != 0:
            rec["error"] = "param without relocation"
        elif layout:
            rec["error"] = "no param"
        rec["value"] = None
        return rec
    rec["off"] = t
    try:
        v, ln, sc = decode(pk, t, layout, pk.extent(t))
        rec.update(value=v, left=pk.extent(t) - ln, scalars=sc, extent_used=ln)
    except LayoutError as e:
        rec["error"] = str(e)
    return rec


def trigger(pk, t, what, depth):
    ty = pk.u32(t)
    lay = TRIGGERS.get(ty, ("?", [("rest", "words")]))[1]
    rec = param_rec(pk, what, ty, t + 4, lay)
    rec["scalars"] = dict(rec.get("scalars", {}), mode=pk.blob[t + 0x28])
    yield rec
    for lo, no in ((0x14, 0x1C), (0x18, 0x20)):
        lst = pk.ptr.get(t + lo)
        cnt = pk.u32(t + no)
        if lst is None:
            continue
        for j in range(min(cnt, 0x100)):
            a = lst + j * 0x18
            at = pk.u32(a)
            lay = ACTIONS.get(at, ("?", [("rest", "words")]))[1]
            r = param_rec(pk, "action", at, a + 4, lay)
            r.setdefault("scalars", {})
            r["scalars"].update({"_delay": pk.u32(a + 0xC), "_count": pk.u32(a + 0x10),
                                 "_w8": pk.u32(a + 8), "_d14": pk.u32(a + 0x14)})
            if "value" in r and "error" not in r:
                import tac_layouts as _t
                ln = r["extent_used"]
                if pk.u32(a + 8) * 4 != ln:
                    r["scalars"]["_w8mismatch"] = (pk.u32(a + 8) * 4, ln)
            yield r
    conds = pk.ptr.get(t + 0xC)
    if conds is not None and depth < 2:
        for j in range(min(pk.u32(t + 0x10), 0x40)):
            yield from trigger(pk, conds + j * 0x2C, "cond", depth + 1)
