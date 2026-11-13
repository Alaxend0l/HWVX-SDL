//! `xdlrs level refuse-test BUILD DATA OUT [CASE ...]` (was tools/level_refuse_test.py): crafted
//! level files the game must refuse. A shared level comes from other players' .xmod mods, so
//! src/game/Port_Level.c treats it as untrusted input and checks everything it asks for.
//!
//! Each case is a sample level (tools/level-samples) made into a mod (`xdlrs level mod`), then
//! changed the way a malicious or broken file would be. One game run per sample world: the
//! game's HWVX_LEVEL_CHECK applies every case's file as the world loads and takes it out again
//! ("levelcheck: FILE: applied" / "...: not applied: WHY"). A case must be refused for the
//! reason it names (a part of the message); the unchanged samples and the `ok-*` cases must
//! apply. The game must keep running (no crash, no Port_Fatal). One game at a time
//! (tools/run_test.sh force-kills its own PID).
//!
//! `--list` lists the cases; CASE names a few (default: all).

use crate::error::{Error, Result};
use serde_json::{Value, json};
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::process::Command;

type Change = fn(&mut Value);

struct Case {
    name: &'static str,
    sample: &'static str,
    change: Option<Change>,
    /// a part of the refusal message (alternatives split by '|'); None: must apply
    why: Option<&'static str>,
}

// ---- the level file's parts -------------------------------------------------------------

fn parts(d: &mut Value) -> &mut Vec<Value> {
    d["parts"].as_array_mut().expect("parts")
}

fn part<'a>(d: &'a mut Value, id: &str) -> &'a mut Value {
    parts(d).iter_mut().find(|p| p["id"] == id).expect("part")
}

fn first_id(d: &Value, kind: &str) -> String {
    d["parts"].as_array().unwrap().iter().find(|p| p["kind"] == kind).map(|p| p["id"].as_str().unwrap().to_string()).expect(kind)
}

fn bytes(p: &Value) -> Vec<u8> {
    let h = p["hex"].as_str().unwrap_or("");
    (0..h.len() / 2).map(|i| u8::from_str_radix(&h[2 * i..2 * i + 2], 16).unwrap_or(0)).collect()
}

fn set_bytes(p: &mut Value, b: &[u8]) {
    p["hex"] = Value::String(b.iter().map(|x| format!("{x:02x}")).collect());
}

fn word(p: &mut Value, off: usize, v: u32) {
    let mut b = bytes(p);
    if b.len() < off + 4 {
        b.resize(off + 4, 0);
    }
    b[off..off + 4].copy_from_slice(&v.to_le_bytes());
    set_bytes(p, &b);
}

fn unlink(p: &mut Value, off: u64) {
    if let Some(l) = p["links"].as_array_mut() {
        l.retain(|x| x[0].as_u64() != Some(off));
    }
}

fn link_part(p: &Value, off: u64) -> Option<String> {

    p["links"].as_array()?.iter().find(|x| x[0].as_u64() == Some(off)).and_then(|x| x[1]["part"].as_str()).map(str::to_string)
}

fn trig_with(d: &Value, off: u64) -> String {
    d["parts"]
        .as_array()
        .unwrap()
        .iter()
        .find(|p| p["kind"] == "trigger" && p["links"].as_array().is_some_and(|l| l.iter().any(|x| x[0].as_u64() == Some(off))))
        .map(|p| p["id"].as_str().unwrap().to_string())
        .expect("a trigger with that link")
}

fn add_part(d: &mut Value, id: &str, kind: &str, b: &[u8], links: Value) {
    let mut p = json!({"id": id, "kind": kind, "hex": "", "links": links});
    set_bytes(&mut p, b);
    parts(d).push(p);
}

fn words_bytes(w: &[u32]) -> Vec<u8> {
    w.iter().flat_map(|x| x.to_le_bytes()).collect()
}

/// A level_start trigger of the level's own with one action of `atype`: its parameters
/// `params` (words), with `links` ([word index, LINK]) written over them.
fn add_action_trigger(d: &mut Value, tag: &str, atype: u32, params: &[u32], links: Value) {
    let pid = format!("x-{tag}-params");
    let aid = format!("x-{tag}-actions");
    let tid = format!("x-{tag}-trigger");
    let pl: Vec<Value> =
        links.as_array().unwrap().iter().map(|l| json!([l[0].as_u64().unwrap() * 4, l[1].clone()])).collect();
    add_part(d, &pid, "data", &words_bytes(params), Value::Array(pl));
    add_part(d, &aid, "data", &words_bytes(&[atype, 0, params.len() as u32, 0, 1, 0]), json!([[4, {"part": pid}]]));
    let mut t = vec![0u32; 11];
    t[0] = 8; // level_start
    t[7] = 1; // numActions
    add_part(d, &tid, "trigger", &words_bytes(&t), json!([[0x14, {"part": aid}]]));
}

/// A trigger of the level's own of `ttype` whose test reads `params`.
fn add_test_trigger(d: &mut Value, tag: &str, ttype: u32, params: &[u32], links: Value) {
    let pid = format!("x-{tag}-params");
    let tid = format!("x-{tag}-trigger");
    let pl: Vec<Value> =
        links.as_array().unwrap().iter().map(|l| json!([l[0].as_u64().unwrap() * 4, l[1].clone()])).collect();
    add_part(d, &pid, "data", &words_bytes(params), Value::Array(pl));
    let mut t = vec![0u32; 11];
    t[0] = ttype;
    add_part(d, &tid, "trigger", &words_bytes(&t), json!([[4, {"part": pid}]]));
}

fn var_slot(d: &Value) -> Value {
    json!({"slot": first_id(d, "variable")})
}

fn world_root(steps: Value) -> Value {
    json!({"root": "world", "index": 0, "steps": steps})
}

// ---- the cases (the first 19: tools/level_refuse_test.py's) -----------------------------

fn bad_trigger_type(d: &mut Value) {
    let t = first_id(d, "trigger");
    word(part(d, &t), 0, 200);
}

fn bad_condition_type(d: &mut Value) {
    let t = trig_with(d, 0x0C);
    let c = link_part(&part(d, &t).clone(), 0x0C).unwrap();
    word(part(d, &c), 0, 99);
}

fn bad_action_type(d: &mut Value) {
    let t = trig_with(d, 0x14);
    let a = link_part(&part(d, &t).clone(), 0x14).unwrap();
    word(part(d, &a), 0, 1000);
}

fn unlinked_param(d: &mut Value) {
    let t = first_id(d, "trigger");
    let p = part(d, &t);
    unlink(p, 4);
    word(p, 4, 0x8000_1000); // an arena handle: an arbitrary address
}

fn unlinked_actions(d: &mut Value) {
    let t = trig_with(d, 0x14);
    let p = part(d, &t);
    unlink(p, 0x14);
    word(p, 0x14, 0x0100_0000); // a static handle (Port_Fatal if followed)
}

fn too_many_actions(d: &mut Value) {
    let t = trig_with(d, 0x14);
    word(part(d, &t), 0x1C, 5000);
}

fn patches(d: &mut Value) -> &mut Vec<Value> {
    if d.get("patches").is_none() {
        d["patches"] = json!([]);
    }
    d["patches"].as_array_mut().unwrap()
}

fn patch_grown(d: &mut Value) {
    patches(d).push(json!({"target": {"append": 0, "elem": 0, "add": 0}, "offset": 0x24, "hex": "00100080"}));
}

fn patch_part(d: &mut Value) {
    let p = first_id(d, "data");
    patches(d).push(json!({"target": {"part": p}, "offset": 0x400, "hex": "41".repeat(64)}));
}

fn patch_pointer(d: &mut Value) {
    patches(d).push(json!({"target": world_root(json!([])), "offset": 0x0C, "hex": "00100080"}));
}

fn patch_long(d: &mut Value) {
    patches(d).push(json!({"target": world_root(json!([])), "offset": 0xA0, "hex": "00".repeat(0x20000)}));
}

fn append_size(d: &mut Value) {
    d["appends"][0]["elemSize"] = json!(0x1000);
}

fn append_table(d: &mut Value) {
    let a = &mut d["appends"][0];
    a["pointer"] = json!(0x6C);
    a["count"] = json!(0x68);
    a["elemSize"] = json!(48);
}

fn append_many(d: &mut Value) {
    let e = d["appends"][0]["elems"][0].clone();
    d["appends"][0]["elems"] = Value::Array(vec![e; 5000]);
}

fn append_set_pointer(d: &mut Value) {
    let a = d["appends"].as_array_mut().unwrap().iter_mut().find(|a| a["pointer"] == 0x18).expect("items");
    let e = &mut a["elems"][0];
    if e.get("set").is_none() {
        e["set"] = json!([]);
    }
    e["set"].as_array_mut().unwrap().push(json!([0x24, "00100080"])); // QuadItem.thing (null in the pack)
}

fn many_variables(d: &mut Value) {
    let v = d["parts"].as_array().unwrap().iter().find(|p| p["kind"] == "variable").unwrap().clone();
    for i in 0..40 {
        let mut n = v.clone();
        n["id"] = json!(format!("{}-x{i}", v["id"].as_str().unwrap()));
        let mut b = bytes(&n);
        b[2] = 200 - i as u8;
        set_bytes(&mut n, &b);
        parts(d).push(n);
    }
}

fn dup_variable_id(d: &mut Value) {
    let vs: Vec<String> =
        d["parts"].as_array().unwrap().iter().filter(|p| p["kind"] == "variable").map(|p| p["id"].as_str().unwrap().to_string()).collect();
    let id0 = bytes(part(d, &vs[0]))[2];
    let p = part(d, &vs[1]);
    let mut b = bytes(p);
    b[2] = id0;
    set_bytes(p, &b);
}

fn timer_no_mode(d: &mut Value) {
    let t = first_id(d, "timer");
    let p = part(d, &t);
    unlink(p, 0x0C);
    word(p, 0x0C, 0);
}

fn volume_bsp_null(d: &mut Value) {
    let v = first_id(d, "volume");
    word(part(d, &v), 0x08, 0); // shape BSP, no BSP
}

fn bad_hex(d: &mut Value) {
    let p = first_id(d, "data");
    let p = part(d, &p);
    let h = format!("{}zz", p["hex"].as_str().unwrap());
    p["hex"] = json!(h);
}

// ---- parameter lists (README §10.6.7: the lists' contents) ------------------------------

fn tac_ok(d: &mut Value) {
    // variable_change_value: set, [the level's variable], constant 1.0: well made
    let l = json!([[2, var_slot(d)]]);
    add_action_trigger(d, "ok", 16, &[17, 1, 0, 0, 1.0f32.to_bits()], l);
}

fn tac_unlinked_handle(d: &mut Value) {
    add_action_trigger(d, "h", 26, &[1, 1, 0x8000_1000], json!([])); // variable_change_mode
}

fn tac_wrong_kind(d: &mut Value) {
    // vehicle_change_mode over a "car" that is a variable's slot
    let l = json!([[2, var_slot(d)]]);
    add_action_trigger(d, "k", 17, &[1, 1, 0], l);
}

fn tac_list_count(d: &mut Value) {
    add_action_trigger(d, "n", 5, &[1, 50, 0], json!([])); // item_change_mode: 50 items, 1 word
}

fn tac_music_track(d: &mut Value) {
    let mut w = vec![0u32; 9];
    w.push(1);
    w.extend([0, 40, 1, 0, 0, 0, 0, 0, 0, 0]); // track 40 of g_tracks[32]
    add_action_trigger(d, "m", 10, &w, json!([]));
}

fn tac_codevar_arg(d: &mut Value) {
    // code_variable_modify_value: set, 0xCB (playerVehicle[arg - 1]) arg 9, constant
    add_action_trigger(d, "c", 34, &[17, 0xCB, 9, 0, 0], json!([]));
}

fn tac_operand_type(d: &mut Value) {
    let l = json!([[2, var_slot(d)]]);
    add_action_trigger(d, "o", 16, &[17, 1, 0, 7, 0], l);
}

fn tac_string_path(d: &mut Value) {
    let mut w = vec![0u32]; // script
    let s = b"../../x\0";
    w.push(u32::from_le_bytes([s[0], s[1], s[2], s[3]]));
    w.push(u32::from_le_bytes([s[4], s[5], s[6], s[7]]));
    add_action_trigger(d, "s", 27, &w, json!([])); // load_interface_pack
}

fn tac_camera_place(d: &mut Value) {
    add_action_trigger(d, "cam", 1, &[1, 30, 9, 0, 0, 0, 0, 0, 0, 0, 31], json!([])); // place kind 9
}

fn tac_opaque_link(d: &mut Value) {
    // interface_play of the level's own bytes (the game reads them as an InterfaceHeader)
    add_part(d, "x-fake-iface", "data", &[0x41; 64], json!([]));
    add_action_trigger(d, "i", 4, &[0], json!([[0, {"part": "x-fake-iface"}]]));
}

fn tac_damage_div0(d: &mut Value) {
    add_test_trigger(d, "dmg", 16, &[0, 43, 0], json!([])); // vehicle_damage below max / 0
}

fn tac_p2p(d: &mut Value) {
    add_test_trigger(d, "p2p", 13, &[0, 1, 0x8000_2000], json!([]));
}

// ---- patches over pointers, counts and indices of other records -------------------------

fn patch_count(d: &mut Value) {
    patches(d).push(json!({"target": world_root(json!([])), "offset": 0x08, "hex": "ff000000"})); // numTileCars
}

fn patch_null_pointer(d: &mut Value) {
    // World.teleporters (null when the world has none: not one of its pointer slots)
    patches(d).push(json!({"target": world_root(json!([])), "offset": 0x34, "hex": "00100080"}));
}

fn patch_index(d: &mut Value) {
    // World/volumes/0.type (VolumeCell lists, creature volumes)
    patches(d).push(json!({"target": world_root(json!([["p", 0x6C]])), "offset": 0x0C, "hex": "05000000"}));
}

fn patch_untyped(d: &mut Value) {
    // the volume grid (VolumeHolder: its counts and cell indices are unbounded): no schema struct
    patches(d).push(json!({"target": world_root(json!([["p", 0x04]])), "offset": 0, "hex": "ffff0000"}));
}

fn patch_trigger_data(d: &mut Value) {
    // what a kept base trigger reads (its actions' records): ch1_l2 keeps none, so:
    d["baseTriggers"] = json!("all");
    patches(d).push(json!({"target": {"root": "trigger", "index": 0, "steps": [["p", 0x14]]}, "offset": 0, "hex": "01000000"}));
}

// ---- objectives and volumes of the level's own -------------------------------------------

fn objective_info(d: &mut Value) -> String {
    let e = first_id(d, "element");
    let ep = part(d, &e).clone();
    link_part(&ep, 4).unwrap()
}

fn objective_null_value(d: &mut Value) {
    let i = objective_info(d);
    let p = part(d, &i);
    let mut b = bytes(p);
    b[0] = 1; // a counter: value read as a WorldVariable
    set_bytes(p, &b);
    unlink(p, 0x10);
    word(p, 0x10, 0);
}

fn objective_long_text(d: &mut Value) {
    let i = objective_info(d);
    let v = first_id(d, "variable");
    let ip = part(d, &i).clone();
    let tab = link_part(&ip, 0x14).unwrap();
    let s = link_part(&part(d, &tab).clone(), 0).unwrap();
    let mut text = vec![b'#'; 60];
    text.push(0);
    set_bytes(part(d, &s), &text);
    let p = part(d, &i);
    let mut b = bytes(p);
    b[0] = 1;
    set_bytes(p, &b);
    p["links"].as_array_mut().unwrap().push(json!([0x10, {"part": v}]));
}

fn objective_ok_counter(d: &mut Value) {
    let i = objective_info(d);
    let v = first_id(d, "variable");
    let p = part(d, &i);
    let mut b = bytes(p);
    b[0] = 1;
    set_bytes(p, &b);
    p["links"].as_array_mut().unwrap().push(json!([0x10, {"part": v}]));
}

fn volume_bsp_escape(d: &mut Value) {
    // a BSP of two nodes whose first jumps 0xFFFF nodes on
    let mut b = vec![0u8; 0x40];
    b[0x10] = 2;
    b[0x12] = 0xFF;
    b[0x13] = 0xFF;
    add_part(d, "x-bsp", "data", &b, json!([]));
    let v = first_id(d, "volume");
    let p = part(d, &v);
    word(p, 0x08, 0);
    p["links"].as_array_mut().unwrap().push(json!([0x28, {"part": "x-bsp"}]));
}

fn volume_bsp_ok(d: &mut Value) {
    // two nodes: the first goes on to the second, which ends
    let mut b = vec![0u8; 0x40];
    b[0x10] = 1;
    add_part(d, "x-bsp", "data", &b, json!([]));
    let v = first_id(d, "volume");
    let p = part(d, &v);
    word(p, 0x08, 0);
    p["links"].as_array_mut().unwrap().push(json!([0x28, {"part": "x-bsp"}]));
}

fn volume_data_short(d: &mut Value) {
    add_part(d, "x-fog", "data", &[0; 4], json!([]));
    let v = first_id(d, "volume");
    let p = part(d, &v);
    word(p, 0x0C, 4); // fog: 16 bytes of data
    p["links"].as_array_mut().unwrap().push(json!([0x2C, {"part": "x-fog"}]));
}

const CASES: &[Case] = &[
    Case { name: "ok-checkpoint-run", sample: "checkpoint-run", change: None, why: None },
    Case { name: "ok-two-gates", sample: "two-gates", change: None, why: None },
    Case { name: "ok-gear-grab", sample: "gear-grab", change: None, why: None },
    Case { name: "ok-flag-run", sample: "flag-run", change: None, why: None },
    Case { name: "ok-short-sprint", sample: "short-sprint", change: None, why: None },
    Case { name: "trigger-type", sample: "checkpoint-run", change: Some(bad_trigger_type), why: Some("no trigger type") },
    Case { name: "condition-type", sample: "checkpoint-run", change: Some(bad_condition_type), why: Some("no type") },
    Case { name: "action-type", sample: "checkpoint-run", change: Some(bad_action_type), why: Some("no type") },
    Case { name: "unlinked-param", sample: "checkpoint-run", change: Some(unlinked_param), why: Some("parameters aren't a link") },
    Case { name: "unlinked-actions", sample: "checkpoint-run", change: Some(unlinked_actions), why: Some("not where the level linked") },
    Case { name: "too-many-actions", sample: "checkpoint-run", change: Some(too_many_actions), why: Some("not where the level linked") },
    Case { name: "patch-grown", sample: "gear-grab", change: Some(patch_grown), why: Some("not in the base world") },
    Case { name: "patch-part", sample: "checkpoint-run", change: Some(patch_part), why: Some("not in the base world|outside") },
    Case { name: "patch-pointer", sample: "checkpoint-run", change: Some(patch_pointer), why: Some("over a pointer") },
    Case { name: "patch-long", sample: "checkpoint-run", change: Some(patch_long), why: Some("outside the record") },
    Case { name: "append-elemsize", sample: "two-gates", change: Some(append_size), why: Some("not an array a level grows") },
    Case { name: "append-table", sample: "two-gates", change: Some(append_table), why: Some("not an array a level grows") },
    Case { name: "append-many", sample: "gear-grab", change: Some(append_many), why: Some("not an array a level grows") },
    Case { name: "append-set-pointer", sample: "gear-grab", change: Some(append_set_pointer), why: Some("writes a pointer") },
    Case { name: "variables-30", sample: "checkpoint-run", change: Some(many_variables), why: Some("more than") },
    Case { name: "variable-id-taken", sample: "checkpoint-run", change: Some(dup_variable_id), why: Some("is taken") },
    Case { name: "timer-no-mode", sample: "two-gates", change: Some(timer_no_mode), why: Some("timer's mode") },
    Case { name: "volume-bsp-null", sample: "checkpoint-run", change: Some(volume_bsp_null), why: Some("BSP") },
    Case { name: "bad-hex", sample: "checkpoint-run", change: Some(bad_hex), why: Some("hex") },
    // the parameter lists' contents
    Case { name: "ok-tac-crafted", sample: "checkpoint-run", change: Some(tac_ok), why: None },
    Case { name: "tac-unlinked-handle", sample: "checkpoint-run", change: Some(tac_unlinked_handle), why: Some("isn't a record of the right kind") },
    Case { name: "tac-wrong-kind", sample: "checkpoint-run", change: Some(tac_wrong_kind), why: Some("isn't a record of the right kind") },
    Case { name: "tac-list-count", sample: "checkpoint-run", change: Some(tac_list_count), why: Some("list runs past") },
    Case { name: "tac-music-track", sample: "checkpoint-run", change: Some(tac_music_track), why: Some("past the 32 tracks") },
    Case { name: "tac-codevar-arg", sample: "checkpoint-run", change: Some(tac_codevar_arg), why: Some("code variable 0xCB") },
    Case { name: "tac-operand-type", sample: "checkpoint-run", change: Some(tac_operand_type), why: Some("no such operand type") },
    Case { name: "tac-string-path", sample: "checkpoint-run", change: Some(tac_string_path), why: Some("path in it") },
    Case { name: "tac-camera-place", sample: "checkpoint-run", change: Some(tac_camera_place), why: Some("no such camera place") },
    Case { name: "tac-opaque-link", sample: "checkpoint-run", change: Some(tac_opaque_link), why: Some("isn't the world's own") },
    Case { name: "tac-damage-div0", sample: "checkpoint-run", change: Some(tac_damage_div0), why: Some("dividing by 0") },
    Case { name: "tac-p2p", sample: "checkpoint-run", change: Some(tac_p2p), why: Some("p2p") },
    // patches over other records' pointers, counts, indices
    Case { name: "patch-count", sample: "checkpoint-run", change: Some(patch_count), why: Some("over a count") },
    Case { name: "patch-null-pointer", sample: "checkpoint-run", change: Some(patch_null_pointer), why: Some("over a pointer") },
    Case { name: "patch-index", sample: "checkpoint-run", change: Some(patch_index), why: Some("indexes with") },
    Case { name: "patch-untyped", sample: "checkpoint-run", change: Some(patch_untyped), why: Some("outside the records") },
    Case { name: "patch-trigger-data", sample: "checkpoint-run", change: Some(patch_trigger_data), why: Some("kept base trigger reads|null in this release") },
    // objectives and volumes of the level's own
    Case { name: "ok-objective-counter", sample: "two-gates", change: Some(objective_ok_counter), why: None },
    Case { name: "objective-null-value", sample: "two-gates", change: Some(objective_null_value), why: Some("done or value") },
    Case { name: "objective-long-text", sample: "two-gates", change: Some(objective_long_text), why: Some("texts") },
    Case { name: "ok-volume-bsp", sample: "checkpoint-run", change: Some(volume_bsp_ok), why: None },
    Case { name: "volume-bsp-escape", sample: "checkpoint-run", change: Some(volume_bsp_escape), why: Some("BSP's nodes") },
    Case { name: "volume-data-short", sample: "checkpoint-run", change: Some(volume_data_short), why: Some("sized for its type") },
];

// ---- running ------------------------------------------------------------------------------

fn io(e: impl std::fmt::Display) -> Error {
    Error::io(e.to_string())
}

pub fn main(pos: &[String]) -> Result<i32> {
    if pos.iter().any(|a| a == "--list") {
        for c in CASES {
            println!("{}", c.name);
        }
        return Ok(0);
    }
    if pos.len() < 3 {
        eprintln!("usage: xdlrs level refuse-test BUILD DATA OUT [CASE ...] | --list");
        return Ok(2);
    }
    let (build, data) = (&pos[0], &pos[1]);
    let out = PathBuf::from(&pos[2]);
    std::fs::create_dir_all(&out).map_err(io)?;
    let out = out.canonicalize().map_err(io)?;
    let names: Vec<&str> = if pos.len() > 3 { pos[3..].iter().map(String::as_str).collect() } else { CASES.iter().map(|c| c.name).collect() };
    let root = crate::repo_root(None).ok_or_else(|| Error::io("no repo root"))?;
    let exe = std::env::current_exe().map_err(io)?;
    // by sample: the mod, the case files
    let mut by: BTreeMap<&str, Vec<(&Case, PathBuf)>> = BTreeMap::new();
    for n in &names {
        let c = CASES.iter().find(|c| c.name == *n).ok_or_else(|| Error::io(format!("no case {n}")))?;
        by.entry(c.sample).or_default().push((c, PathBuf::new()));
    }
    let mut fails = 0;
    for (sample, cases) in by.iter_mut() {
        let mods = out.join(format!("mods-{sample}"));
        let modd = mods.join(sample);
        let src = root.join("tools/level-samples").join(sample);
        let st = Command::new(&exe).arg("level").arg("mod").arg(&src).arg(&modd).output().map_err(io)?;
        if !st.status.success() {
            return Err(Error::io(format!("{sample}: xdlrs level mod: {}", String::from_utf8_lossy(&st.stderr))));
        }
        let lvf = std::fs::read_dir(modd.join("level"))
            .map_err(io)?
            .filter_map(|e| e.ok().map(|e| e.path()))
            .find(|p| p.extension().is_some_and(|x| x == "json"))
            .ok_or_else(|| Error::io("no level file"))?;
        let doc = crate::jsonc::parse(&std::fs::read_to_string(&lvf).map_err(io)?)?;
        let lv = crate::jsonc::loads(&std::fs::read_to_string(src.join("level.jsonc")).map_err(io)?)?;
        let mode = lv["mode"].as_str().unwrap_or("challenge").to_string();
        let mut list = String::new();
        for (c, f) in cases.iter_mut() {
            let mut d = doc.clone();
            if let Some(ch) = c.change {
                ch(&mut d);
            }
            *f = out.join(format!("{}.json", c.name));
            std::fs::write(&*f, serde_json::to_string(&d).unwrap()).map_err(io)?;
            list.push_str(&format!("{}\n", f.display()));
        }
        let lst = out.join(format!("check-{sample}.txt"));
        std::fs::write(&lst, list).map_err(io)?;
        let name = format!("check-{sample}");
        let r = Command::new(root.join("tools/run_test.sh"))
            .args([build.as_str(), data.as_str(), out.to_str().unwrap(), &name, "14"])
            .arg(format!("HWVX_MODS_DIR={}", mods.display()))
            .arg(format!("HWVX_LEVEL_CHECK={}", lst.display()))
            .arg(format!("HWVX_START=race:mode={mode},world={}", doc["id"].as_str().unwrap_or("")))
            .arg("HWVX_AUTOINPUT=200:a,300:a,400:a")
            .env("FAST", "1")
            .output()
            .map_err(io)?;
        let stdout = String::from_utf8_lossy(&r.stdout);
        let alive = stdout.contains("ran ");
        let log = std::fs::read_to_string(out.join(format!("{name}.log"))).unwrap_or_default();
        for (c, f) in cases.iter() {
            let key = format!("levelcheck: {}: ", f.display());
            let line = log.lines().find_map(|l| l.split_once(&key).map(|x| x.1.to_string()));
            let (ok, why) = match (&line, c.why) {
                (None, _) => (false, "never checked (did the world load?)".to_string()),
                (Some(l), None) => (l == "applied", l.clone()),
                (Some(l), Some(w)) => {
                    let r = l.strip_prefix("not applied: ").unwrap_or("");
                    (!r.is_empty() && w.split('|').any(|x| r.contains(x)), l.clone())
                }
            };
            let ok = ok && alive;
            println!("{} {}: {}{}", if ok { "ok  " } else { "FAIL" }, c.name, why, if alive { "" } else { " (the game died)" });
            fails += !ok as i32;
        }
    }
    Ok(if fails > 0 { 1 } else { 0 })
}
