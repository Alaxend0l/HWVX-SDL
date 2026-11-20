//! Levels made from a template: a Joyride world as the stage of a level of any mode
//! (docs/xdl-format/README.md §10.6, tools/browser/docs-joyride-templates.md).
//!
//! A Joyride level is a whole open world with a script of free roam: its intro, its music and
//! effects, and its collectibles, ten gears and a hidden key (each a pickup point, `QuadItem`,
//! of some quadrant; `item_pickup` triggers count the gears in a variable and unlock a car at ten,
//! another unlocks a car for the key; two objectives, "Collect 10 GEARS to UNLOCK CAR" and "Find
//! Hidden KEY to UNLOCK CAR", show on the intro box). [`collectibles`] finds them in a base world
//! from its script, so the same rule holds on every Joyride world (and on any world whose script
//! counts pickups): the `item_pickup` triggers and the triggers that unlock a car (code variable
//! 100), then every trigger that reads or writes a variable those do, until none is added; their
//! pickup points; the objectives whose "done" variable is one of those variables.
//!
//! [`new_level`] writes a level project: of `mode`, extending the mode's first retail level (its
//! loading screen, win and lose sequences: the progression entry the game copies), on `world`.
//! On another mode's world (a template) it also gets what that world's script doesn't give the
//! mode: a clock on the HUD and a time-out (challenge, mission), round rules and the round flow
//! (battle, drag race: a Joyride world has no round screens), and a briefing line in the Joyride
//! intro box (an objective of the level's own with the HUD id of the removed one). With `clean`,
//! the template's collectibles are removed (`remove`, `baseTriggers` without their triggers).

use crate::error::{Error, Kind, Result};
use crate::level::{BaseWorld, MODES, read_jsonc};
use serde_json::{Map, Value, json};
use std::collections::{BTreeSet, HashSet};
use std::path::{Path, PathBuf};

fn lerr(m: impl Into<String>) -> Error {
    Error::new(Kind::Level, m.into())
}

/// What a world's script collects, by base link (base://ARCHIVE/world_0/...).
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Collectibles {
    /// the pickup points the counting triggers name (gears, the key)
    pub items: Vec<String>,
    /// the objectives that show the count (their "done" variable is one the triggers keep)
    pub objectives: Vec<String>,
    /// the base triggers that count, unlock and show them (by number)
    pub triggers: Vec<i64>,
    /// the other base triggers (intro, music, effects): what `baseTriggers` keeps
    pub kept: Vec<i64>,
}

/// Every string in v (links are strings).
fn strings<'a>(v: &'a Value, out: &mut Vec<&'a str>) {
    match v {
        Value::String(s) => out.push(s),
        Value::Array(a) => a.iter().for_each(|x| strings(x, out)),
        Value::Object(m) => m.values().for_each(|x| strings(x, out)),
        _ => {}
    }
}

/// Does v hold a code variable reference {"var": 100, ...} (a car unlock)?
fn unlocks_a_car(v: &Value) -> bool {
    match v {
        Value::Array(a) => a.iter().any(unlocks_a_car),
        Value::Object(m) => {
            m.get("var").and_then(|x| x.get("var")).and_then(Value::as_i64) == Some(100) || m.values().any(unlocks_a_car)
        }
        _ => false,
    }
}

fn index_after<'a>(s: &'a str, key: &str) -> Option<&'a str> {
    let i = s.find(key)? + key.len();
    let rest = &s[i..];
    let n = rest.bytes().take_while(u8::is_ascii_digit).count();
    (n > 0).then(|| &rest[..n])
}

/// The collectibles of the base world extracted at `base` (`xdlrs level base`).
pub fn collectibles(base: &Path) -> Result<Collectibles> {
    let b = BaseWorld::open(base)?;
    let pre = format!("base://{}/world_0/", b.archive);
    // per trigger: its type, the pickup points it names, the variables it names, a car unlock
    let mut trigs: Vec<(i64, String, Vec<String>, BTreeSet<String>, bool)> = vec![];
    for (n, f) in b.trigger_files_pub() {
        let d = read_jsonc(&f)?;
        let ty = d.get("type").and_then(Value::as_str).unwrap_or("").to_string();
        // its own parts only (children are the data its parameters point at)
        let mut own = d.clone();
        if let Some(m) = own.as_object_mut() {
            m.remove("children");
            m.remove("id");
        }
        let mut ss = vec![];
        strings(&own, &mut ss);
        let mut items = vec![];
        let mut vars = BTreeSet::new();
        for s in ss {
            let Some(rel) = s.strip_prefix(&pre) else { continue };
            if rel.starts_with("World/0/quads/") && rel.contains("/contents/items/") {
                items.push(s.to_string());
            } else if let Some(v) = index_after(rel, "World/0/variables/") {
                vars.insert(v.to_string());
            }
        }
        trigs.push((n as i64, ty, items, vars, unlocks_a_car(&own)));
    }
    let mut dropped: BTreeSet<i64> = trigs.iter().filter(|t| t.1 == "item_pickup" || t.4).map(|t| t.0).collect();
    let mut vars: BTreeSet<String> = BTreeSet::new();
    loop {
        for t in &trigs {
            if dropped.contains(&t.0) {
                vars.extend(t.3.iter().cloned());
            }
        }
        let more: Vec<i64> = trigs.iter().filter(|t| !dropped.contains(&t.0) && t.3.iter().any(|v| vars.contains(v))).map(|t| t.0).collect();
        if more.is_empty() {
            break;
        }
        dropped.extend(more);
    }
    let mut items: Vec<String> = vec![];
    let mut seen = HashSet::new();
    for t in &trigs {
        if dropped.contains(&t.0) && t.1 == "item_pickup" {
            for i in &t.2 {
                if seen.insert(i.clone()) {
                    items.push(i.clone());
                }
            }
        }
    }
    // objectives: their info's done variable (link at 0xC), in the World's objectives
    let mut objectives = vec![];
    let of = b.dir.join("World").join("0").join("objectives.base.jsonc");
    if of.exists() {
        let d = read_jsonc(&of)?;
        let n = d.get("count").and_then(Value::as_u64).unwrap_or(0);
        for k in 0..n {
            let info = format!("{pre}World/0/objectives/{k}/info");
            let done = d.get("children").and_then(|c| c.get(&info)).and_then(|i| i.get("links")).and_then(|l| l.get("0xC")).and_then(Value::as_str);
            let var = done.and_then(|l| l.strip_prefix(&pre)).and_then(|r| index_after(r, "World/0/variables/"));
            if var.is_some_and(|v| vars.contains(v)) {
                objectives.push(format!("{pre}World/0/objectives/{k}"));
            }
        }
    }
    let kept = trigs.iter().map(|t| t.0).filter(|n| !dropped.contains(n)).collect();
    Ok(Collectibles { items, objectives, triggers: dropped.into_iter().collect(), kept })
}

/// The HUD object ids the objectives show in (WorldObjectiveInfo.id, byte 2 of the info), in order.
pub fn objective_hud_ids(base: &Path, objectives: &[String]) -> Result<Vec<u8>> {
    let b = BaseWorld::open(base)?;
    let d = read_jsonc(&b.dir.join("World").join("0").join("objectives.base.jsonc"))?;
    let mut out = vec![];
    for o in objectives {
        let bytes = d.get("children").and_then(|c| c.get(format!("{o}/info"))).and_then(|i| i.get("bytes"));
        let first = match bytes {
            Some(Value::Array(a)) => a.first().and_then(Value::as_str),
            Some(Value::String(s)) => Some(s.as_str()),
            _ => None,
        };
        if let Some(id) = first.and_then(|h| h.get(4..6)).and_then(|x| u8::from_str_radix(x, 16).ok()) {
            out.push(id);
        }
    }
    Ok(out)
}

/// Is `world` (as the MCP names it, worlds_\sp_\joyride_\world4) a Joyride level's world?
pub fn is_joyride_world(world: &str) -> bool {
    world.to_lowercase().replace('/', "\\").contains("\\joyride_\\")
}

/// What [`new_level`] makes.
#[derive(Debug, Clone)]
pub struct NewLevel {
    /// challenge, mission, battle, drag, joyride
    pub mode: String,
    /// the world's MCP path
    pub world: String,
    /// the retail level it extends: for a level on the mode's own world that level's track,
    /// else the mode's first (its progression entry, which every level of the mode shares)
    pub extends: u32,
    pub name: String,
    /// the level's folder (made; must not hold a level.jsonc)
    pub dir: PathBuf,
    /// on a template: its collectibles removed
    pub clean: bool,
    /// the base world extracted (`xdlrs level base`): needed for `clean` and the template's script
    pub base: Option<PathBuf>,
}

/// The mode's first retail level (its progression entry).
pub fn first_track(mode: &str) -> Option<u32> {
    MODES.iter().find(|m| m.0 == mode).map(|m| m.1)
}

fn trigger(ty: &str, param: Value, conditions: Value, actions: Value) -> Value {
    let mut m = Map::new();
    m.insert("$schema".into(), json!("../../../../schemas/Trigger.schema.json"));
    m.insert("format_version".into(), json!(1));
    m.insert("type".into(), json!(ty));
    if !param.is_null() {
        m.insert("param".into(), param);
    }
    if !conditions.is_null() {
        m.insert("conditions".into(), conditions);
    }
    m.insert("actions".into(), actions);
    Value::Object(m)
}

fn var_is(var: &str, v: i64) -> Value {
    json!({"type": "variable_value_check", "param": {"variables": [var], "value": {"const": v}, "op": "equal"}})
}

fn set_var(var: &str, v: i64) -> Value {
    json!({"type": "variable_change_value", "data": {"op": "set", "variables": [var], "value": {"const": v}}})
}

/// The level project's files: level.jsonc's text and the triggers (name, document).
pub fn template_files(o: &NewLevel) -> Result<(String, Vec<(String, Value)>)> {
    if !MODES.iter().any(|m| m.0 == o.mode) || o.mode == "adventure" {
        return Err(lerr(format!("mode: one of challenge, mission, battle, drag, joyride (not {})", o.mode)));
    }
    let id: String = o.name.to_lowercase().chars().filter(|c| c.is_ascii_alphanumeric()).take(13).collect();
    let template = is_joyride_world(&o.world) && o.mode != "joyride";
    let mut lv = Map::new();
    lv.insert("$schema".into(), json!("../../schemas/Level.schema.json"));
    lv.insert("format_version".into(), json!(1));
    lv.insert("id".into(), json!(format!("uid://{}", if id.is_empty() { "mylevel".into() } else { id })));
    lv.insert("name".into(), json!(o.name.trim()));
    lv.insert("mode".into(), json!(o.mode));
    lv.insert("extends".into(), json!(format!("base://mcp/track/{}", o.extends)));
    lv.insert("world".into(), json!(o.world));
    lv.insert("unlocked".into(), json!(true));
    lv.insert("baseTriggers".into(), json!("all"));
    let mut trigs: Vec<(String, Value)> = vec![];
    let mut notes: Vec<String> = vec![];
    let archive = o.world.replace('/', "\\").rsplit('\\').next().unwrap_or("").to_lowercase();
    let car0 = format!("base://{archive}/world_0/World/0/tileCars/0");
    let mut hud: Option<u8> = None;
    // music the dropped intro triggers played: at the start, and when the intro (or countdown) ends
    let mut music_start: Vec<Value> = vec![];
    let mut music_go: Vec<Value> = vec![];
    if template && o.base.is_none() {
        return Err(lerr("a level on a Joyride world is made from its base world (--data): its script and collectibles"));
    }
    if let Some(base) = &o.base {
        let b = BaseWorld::open(base)?;
        if b.archive.to_lowercase() != archive {
            return Err(lerr(format!("the base given is {}, not the level's world {archive}", b.archive)));
        }
        let c = if o.clean { Some(collectibles(base)?) } else { None };
        let mut kept: Vec<i64> = match &c {
            Some(c) => c.kept.clone(),
            None => b.trigger_files_pub().iter().map(|x| x.0 as i64).collect(),
        };
        if template {
            // the Joyride script's intro (its interface and what waits for it) goes: the mode's own
            // screens open the level; its music is kept in the level's start and go
            let mut drop = HashSet::new();
            for (n, f) in b.trigger_files_pub() {
                let d = read_jsonc(&f)?;
                let mut own = d.clone();
                if let Some(m) = own.as_object_mut() {
                    m.remove("children");
                }
                if !own.to_string().contains("\"interface\"") {
                    continue;
                }
                drop.insert(n as i64);
                let ty = d.get("type").and_then(Value::as_str).unwrap_or("");
                for a in d.get("actions").and_then(Value::as_array).cloned().unwrap_or_default() {
                    if a.get("type").and_then(Value::as_str) == Some("music_change") {
                        let mut a = a.clone();
                        if let Some(m) = a.as_object_mut() {
                            m.retain(|k, _| k == "type" || k == "data");
                        }
                        if ty == "interface_end" { music_go.push(a) } else { music_start.push(a) }
                    }
                }
            }
            kept.retain(|n| !drop.contains(n));
        }
        if template || c.is_some() {
            lv.insert("baseTriggers".into(), json!(kept));
        }
        if let Some(c) = c.filter(|c| !c.items.is_empty() || !c.triggers.is_empty()) {
            let mut rm = Map::new();
            rm.insert("items".into(), json!(c.items));
            if !c.objectives.is_empty() {
                rm.insert("objectives".into(), json!(c.objectives));
            }
            lv.insert("remove".into(), Value::Object(rm));
            hud = objective_hud_ids(base, &c.objectives)?.into_iter().min();
            notes.push(format!(
                "the template's {} pickups (the gears and the key), {} objectives and {} triggers that count them removed",
                c.items.len(),
                c.objectives.len(),
                c.triggers.len()
            ));
        }
    }
    if template {
        let sc = mode_screens(&o.mode).ok_or_else(|| lerr(format!("no screens for mode {}", o.mode)))?;
        let scr = |p: &str| format!("base://{}/world_0/{p}", sc.archive);
        lv.insert("modeWorld".into(), json!(sc.world));
        let mut strings = Map::new();
        let mut vars = Map::new();
        let mut timers = Map::new();
        vars.insert("done".into(), json!(0));
        let cars = json!([car0]);
        let orbit = json!({"type": "camera_set_targets", "data": {"player": 0, "camera": {"kind": "orbit", "fov": 105, "radius": 18, "height": 0.5, "speed": 1,
            "around": {"kind": "car", "car": car0, "offset": [10, 10, 0]}, "at": {"kind": "car", "car": car0, "offset": [0, 0, 0]}}, "end": 31}});
        let behind = json!({"type": "camera_set_targets", "data": {"player": 0, "camera": {"kind": "player",
            "from": {"kind": "car", "car": car0, "offset": [0, 0, 0]}, "at": {"kind": "car", "car": car0, "offset": [0, 0, 0]}}, "end": 31}});
        let act = |ty: &str, data: Value, delay: i64| {
            let mut a = json!({"type": ty, "data": data});
            if delay > 0 {
                a["delayInit"] = json!(delay);
            }
            a
        };
        let play = |p: &str, delay: i64| act("interface_play", json!({"interface": scr(p)}), delay);
        let hud_on = |on: i64, delay: i64| act("hud_change_mode", json!({"unused": 1, "on": on}), delay);
        let control = |mode: &str, delay: i64| act("vehicle_change_control", json!({"mode": mode, "unused": 0, "cars": cars}), delay);
        let clock = |t: &str, show: &str, run: &str, delay: i64| act("timer_change_mode", json!({"show": show, "run": run, "timers": [format!("level://timers/{t}")]}), delay);
        let always = |mut a: Vec<Value>| {
            for x in a.iter_mut() {
                x["count"] = json!("always");
            }
            json!(a)
        };
        match o.mode.as_str() {
            "challenge" | "mission" => {
                // the mode's own intro box, HUD start, Objective Complete and Objective Failed, as
                // its retail levels play them (checkpoint-run's flow); the briefing line is an
                // objective of the level's own (the intro box shows it)
                let brief = if o.mode == "challenge" { "Beat the clock!" } else { "Your mission starts here" };
                strings.insert("blank".into(), json!("     "));
                if let Some(h) = hud {
                    strings.insert("brief".into(), json!(brief));
                    vars.insert("briefed".into(), json!(0));
                    lv.insert(
                        "objectives".into(),
                        json!({"brief": {"text": "level://strings/brief", "doneText": "level://strings/brief", "kind": "none", "done": "level://variables/briefed/to", "hud": h}}),
                    );
                }
                vars.insert("goal".into(), json!(0));
                timers.insert("clock".into(), json!({"time": 60000, "countUp": 0, "type": 12}));
                let mut start = vec![
                    act("print_debug_string", json!({"what": 4, "countdown": 60, "value": "level://strings/blank"}), 0),
                    orbit.clone(),
                    { let mut b = behind.clone(); b["delayInit"] = json!(3500); b },
                    hud_on(0, 0),
                    act("vehicle_change_mode", json!({"mode": "ai_hold", "cars": cars}), 0),
                    play(sc.intro, 2000),
                    clock("clock", "show", "pause", 0),
                ];
                start.extend(music_start.iter().cloned());
                trigs.push(("start".into(), trigger("level_start", Value::Null, Value::Null, json!(start))));
                let mut go = vec![play(sc.hud, 0), hud_on(1, 1000), control("lock_control", 0), control("unlock_control", 3500), clock("clock", "show", "resume", 3500)];
                go.extend(music_go.iter().cloned());
                trigs.push(("go".into(), trigger("interface_end", json!({"interface": scr(sc.intro)}), Value::Null, json!(go))));
                trigs.push((
                    "won".into(),
                    trigger(
                        "variable_value_check",
                        json!({"variables": ["level://variables/goal"], "value": {"const": 1}, "op": "equal"}),
                        json!([var_is("level://variables/done", 0)]),
                        json!([
                            set_var("level://variables/done", 1),
                            set_var("level://variables/briefed", 1),
                            clock("clock", "show", "pause", 0),
                            control("lock_control", 1000),
                            orbit.clone(),
                            hud_on(0, 200),
                            play(sc.complete, 4000)
                        ]),
                    ),
                ));
                trigs.push((
                    "timeout".into(),
                    trigger(
                        "timer_value_check",
                        json!({"timers": ["level://timers/clock"], "value": {"const": 0}, "op": "equal"}),
                        json!([var_is("level://variables/done", 0)]),
                        json!([set_var("level://variables/done", 1), play(sc.failed, 2000), control("lock_control", 0), orbit.clone(), hud_on(0, 200)]),
                    ),
                ));
                notes.push("add what wins it: a trigger that sets level://variables/goal to 1 (a volume to reach, items to pick up); won plays the mode's Objective Complete".into());
            }
            _ => {
                // the mode's countdown and results screens, as its retail levels play them: the
                // countdown at the start of every round; after a round the results (who won it,
                // the standings), and when they're closed the next round; after the last, the
                // results end the level as retail's do
                let battle = o.mode == "battle";
                timers.insert("clock".into(), json!({"time": if battle { 120000 } else { 30000 }, "countUp": 0, "type": 12}));
                lv.insert(
                    "rules".into(),
                    if battle {
                        json!({"mode": "kills", "kills": 5, "rounds": 3, "time": 120000, "roundTimer": "level://timers/clock"})
                    } else {
                        json!({"mode": "none", "rounds": 1, "time": 30000, "roundTimer": "level://timers/clock"})
                    },
                );
                let mut start = vec![
                    play(sc.countdown, if battle { 0 } else { 500 }),
                    control("lock_control", 0),
                    control("unlock_control", if battle { 3500 } else { 4500 }),
                    hud_on(0, 0),
                    hud_on(1, if battle { 1000 } else { 4500 }),
                    clock("clock", "show", "pause", 0),
                    clock("clock", "show", "resume", if battle { 3500 } else { 4500 }),
                    act("code_variable_modify_value", json!({"op": "set", "var": {"var": 300, "arg": 0}, "value": {"const": 1}}), 0),
                ];
                start.extend(music_start.iter().cloned());
                trigs.push(("start".into(), trigger("level_start", Value::Null, Value::Null, always(start))));
                for (n, ty) in [("round-over", "game_round_x_complete"), ("series-over", "game_rounds_complete")] {
                    trigs.push((
                        n.into(),
                        trigger(ty, Value::Null, Value::Null, always(vec![hud_on(0, 0), control("lock_control", 0), clock("clock", "show", "pause", 0), play(sc.results, 3000)])),
                    ));
                }
                let mut next = vec![
                    act("reset_game_round", json!({"resetMotion": 1, "resetPosition": 1, "jettison": battle as i64}), 0),
                    hud_on(1, 1000),
                    control("unlock_control", 3500),
                    play(sc.countdown, 0),
                    clock("clock", "show", "resume", 3500),
                ];
                next.extend(music_go.iter().cloned());
                trigs.push(("next-round".into(), trigger("interface_end", json!({"interface": scr(sc.results)}), Value::Null, always(next))));
                if !battle {
                    // the clock: out of time, the round goes to player 2
                    trigs.push((
                        "timeout".into(),
                        trigger(
                            "timer_value_check",
                            json!({"timers": ["level://timers/clock"], "value": {"const": 0}, "op": "equal"}),
                            json!([var_is("level://variables/done", 0)]),
                            always(vec![set_var("level://variables/done", 1), play(sc.out_of_time, 0), act("round_complete", json!({"player": 2}), 2000)]),
                        ),
                    ));
                }
                notes.push(if battle {
                    "add a start for each opponent (port 0: an AI car; a battle needs 2 starts at least), and them to start's, round-over's and next-round's cars".into()
                } else {
                    format!(
                        "add 3 starts, one per lane (4 in all), a finish volume and a trigger per car: round_complete N and the mode's \"Player N WINS\" ({}, {}, {}, {})",
                        scr(sc.wins[0]),
                        scr(sc.wins[1]),
                        scr(sc.wins[2]),
                        scr(sc.wins[3])
                    )
                });
            }
        }
        if !strings.is_empty() {
            lv.insert("strings".into(), Value::Object(strings));
        }
        lv.insert("variables".into(), Value::Object(vars));
        lv.insert("timers".into(), Value::Object(timers));
    } else if o.mode == "battle" {
        lv.insert("rules".into(), json!({"mode": "kills", "kills": 5, "rounds": 3, "time": 120000}));
    }
    lv.insert("triggers".into(), json!(trigs.iter().map(|t| t.0.clone()).collect::<Vec<_>>()));
    let what = if template { format!("on {} (a Joyride world as its template)", o.world) } else { format!("on {}", o.world) };
    let mut text = format!("// a {} level, made in the level editor {what}\n", o.mode);
    for n in &notes {
        text.push_str(&format!("// {n}\n"));
    }
    text.push_str(&serde_json::to_string_pretty(&Value::Object(lv)).map_err(|e| Error::uncaught(e.to_string()))?);
    text.push('\n');
    Ok((text, trigs))
}

/// Writes the project (never over a level that's there: level.jsonc made new or not at all).
pub fn new_level(o: &NewLevel) -> Result<PathBuf> {
    let (text, trigs) = template_files(o)?;
    let tdir = o.dir.join("triggers");
    std::fs::create_dir_all(&tdir).map_err(|e| Error::io(format!("{}: {e}", tdir.display())))?;
    {
        use std::io::Write;
        let f = o.dir.join("level.jsonc");
        let mut h = std::fs::OpenOptions::new().write(true).create_new(true).open(&f).map_err(|e| {
            if e.kind() == std::io::ErrorKind::AlreadyExists {
                lerr(format!("{} already has a level: open it, or pick another name", o.dir.display()))
            } else {
                Error::io(format!("{}: {e}", f.display()))
            }
        })?;
        h.write_all(text.as_bytes()).map_err(|e| Error::io(format!("{}: {e}", f.display())))?;
    }
    for (n, d) in trigs {
        let p = tdir.join(format!("{n}.jsonc"));
        let t = serde_json::to_string_pretty(&d).map_err(|e| Error::uncaught(e.to_string()))?;
        std::fs::write(&p, format!("{t}\n")).map_err(|e| Error::io(format!("{}: {e}", p.display())))?;
    }
    Ok(o.dir.clone())
}

/// A mode's own screens, in its first retail level's world pack (the level file's `modeWorld`, which the
/// game loads beside another mode's world): the same screens in every retail level of the mode
/// (`screens`: their shapes are one per role across all of a mode's levels; world 5's levels have
/// another font texture), by the paths its triggers name them with.
pub struct ModeScreens {
    pub world: &'static str,
    pub archive: &'static str,
    /// challenge, mission: the intro box, the HUD start, Objective Complete, Objective Failed
    pub intro: &'static str,
    pub hud: &'static str,
    pub complete: &'static str,
    pub failed: &'static str,
    /// battle, drag: the countdown and the round results (standings)
    pub countdown: &'static str,
    pub results: &'static str,
    /// drag: "Player N WINS!!", "Out of Time"
    pub wins: [&'static str; 4],
    pub out_of_time: &'static str,
}

pub fn mode_screens(mode: &str) -> Option<ModeScreens> {
    let none = "";
    Some(match mode {
        "challenge" => ModeScreens {
            world: "worlds_\\sp_\\chal1_\\ch1_l2",
            archive: "ch1_l2",
            intro: "trigger/18/param/p0",
            hud: "trigger/18/actions/0/data/p0",
            complete: "trigger/15/actions/4/data/p0",
            failed: "trigger/15/actions/12/data/p4",
            countdown: none,
            results: none,
            wins: [none; 4],
            out_of_time: none,
        },
        "mission" | "adventure" => ModeScreens {
            world: "WORLDS_\\SP_\\MISS_\\world1l1",
            archive: "world1l1",
            intro: "trigger/23/param/p0",
            hud: "trigger/23/actions/3/data/p0",
            complete: "trigger/35/actions/0/data/p0",
            failed: "trigger/36/actions/1/data/p0",
            countdown: none,
            results: none,
            wins: [none; 4],
            out_of_time: none,
        },
        "battle" => ModeScreens {
            world: "WORLDS_\\MP_\\BATTLE_\\bat1_l1",
            archive: "bat1_l1",
            intro: none,
            hud: none,
            complete: none,
            failed: none,
            countdown: "trigger/0/actions/3/data/p0",
            results: "trigger/0/param/p0",
            wins: [none; 4],
            out_of_time: none,
        },
        "drag" => ModeScreens {
            world: "WORLDS_\\SP_\\DRAG_\\race1l2",
            archive: "race1l2",
            intro: none,
            hud: none,
            complete: none,
            failed: none,
            countdown: "trigger/26/actions/1/data/p0",
            results: "trigger/16/param/p0",
            wins: ["trigger/10/actions/1/data/p0", "trigger/11/actions/1/data/p0", "trigger/12/actions/1/data/p0", "trigger/13/actions/1/data/p0"],
            out_of_time: "trigger/10/actions/4/data/p4",
        },
        _ => return None,
    })
}

/// A screen (an interface) a world's triggers play or wait for.
#[derive(Debug, Clone)]
pub struct Screen {
    /// its node, relative to the world (trigger/15/actions/4/data/p0)
    pub path: String,
    /// its fingerprint: the bytes of every record it reaches (slots zeroed), the shape of its
    /// links and its textures' and sounds' own bytes, whatever their numbers and paths in this pack
    pub hash: String,
    /// the texts it holds (Objective Complete, Retry...)
    pub texts: Vec<String>,
}

fn printable_runs(b: &[u8], out: &mut Vec<String>) {
    let mut cur = String::new();
    for &c in b.iter().chain(std::iter::once(&0u8)) {
        if (0x20..0x7f).contains(&c) {
            cur.push(c as char);
        } else {
            if cur.len() >= 4 && cur.chars().any(|c| c.is_ascii_alphabetic()) && !cur.contains("?ff") {
                out.push(std::mem::take(&mut cur));
            }
            cur.clear();
        }
    }
}

/// The screens of the base world extracted at `base`, fingerprinted so that the same screen in
/// two levels' packs (other node numbers, other texture numbers) has the same hash.
/// `assets`: false hashes the shape only (every texture and sound alike): the same screen with
/// another font texture or picture.
pub fn screens(env: &crate::level::Env, base: &Path, assets: bool) -> Result<Vec<Screen>> {
    use std::collections::HashMap;
    use std::hash::{Hash, Hasher};
    let b = BaseWorld::open(base)?;
    let pre = format!("base://{}/world_0/", b.archive);
    let mut builder = crate::graph::Builder::new(&env.g, base)?;
    let layout: Vec<Value> = builder.pack["layout"].as_array().cloned().unwrap_or_default();
    let by_id: HashMap<String, Value> = layout.iter().map(|e| (e["id"].as_str().unwrap_or("").to_string(), e.clone())).collect();
    // the roots: what trigger parameters name as an interface
    let mut roots: BTreeSet<String> = BTreeSet::new();
    fn ifaces(v: &Value, out: &mut BTreeSet<String>) {
        match v {
            Value::Object(m) => {
                if let Some(Value::String(s)) = m.get("interface") {
                    out.insert(s.clone());
                }
                m.values().for_each(|x| ifaces(x, out));
            }
            Value::Array(a) => a.iter().for_each(|x| ifaces(x, out)),
            _ => {}
        }
    }
    for (_, f) in b.trigger_files_pub() {
        let d = read_jsonc(&f)?;
        let mut d = d;
        if let Some(m) = d.as_object_mut() {
            m.remove("children");
        }
        ifaces(&d, &mut roots);
    }
    // a texture's or sound's own bytes: the files of its folder
    let asset = |id: &str| -> u64 {
        if !assets {
            return 0xA55E7;
        }
        let rel = id.strip_prefix(&pre).unwrap_or(id);
        let mut h = std::collections::hash_map::DefaultHasher::new();
        let dir = b.dir.join(rel);
        let mut files: Vec<PathBuf> = std::fs::read_dir(&dir).map(|r| r.flatten().map(|e| e.path()).collect()).unwrap_or_default();
        files.sort();
        for f in files.iter().filter(|f| f.extension().is_some_and(|e| e == "bin")) {
            std::fs::read(f).unwrap_or_default().hash(&mut h);
        }
        h.finish()
    };
    let mut memo: HashMap<String, u64> = HashMap::new();
    let mut out = vec![];
    for r in roots {
        let mut texts = vec![];
        // depth first, each node once per screen (a cycle hashes as a back reference)
        fn walk(
            id: &str,
            builder: &mut crate::graph::Builder,
            by_id: &HashMap<String, Value>,
            memo: &mut HashMap<String, u64>,
            on: &mut HashSet<String>,
            texts: &mut Vec<String>,
            asset: &dyn Fn(&str) -> u64,
        ) -> Result<u64> {
            if let Some(h) = memo.get(id) {
                return Ok(*h);
            }
            let mut h = std::collections::hash_map::DefaultHasher::new();
            if id.contains("/texture/") || id.contains("/sound/") {
                let v = asset(id);
                return Ok(v);
            }
            if !on.insert(id.to_string()) {
                return Ok(0xC1C1E);
            }
            let Some(ent) = by_id.get(id) else {
                id.hash(&mut h); // outside the pack (another archive): by name
                return Ok(h.finish());
            };
            let Some(n) = builder.built(ent)? else { return Ok(0) };
            let mut data = n.data.clone();
            for off in n.links.keys().chain(n.refs.keys()).chain(n.tex.keys()).chain(n.snd.keys()) {
                for k in 0..4 {
                    if let Some(x) = data.get_mut(*off as usize + k) {
                        *x = 0;
                    }
                }
            }
            printable_runs(&n.data, texts);
            data.hash(&mut h);
            for (off, to) in n.links.iter().chain(n.refs.iter()).chain(n.tex.iter()).chain(n.snd.iter()) {
                off.hash(&mut h);
                walk(to, builder, by_id, memo, on, texts, asset)?.hash(&mut h);
            }
            let v = h.finish();
            on.remove(id);
            memo.insert(id.to_string(), v);
            Ok(v)
        }
        let mut on = HashSet::new();
        let h = walk(&r, &mut builder, &by_id, &mut memo, &mut on, &mut texts, &asset)?;
        let mut seen = HashSet::new();
        texts.retain(|t| seen.insert(t.clone()));
        out.push(Screen { path: r.strip_prefix(&pre).unwrap_or(&r).to_string(), hash: format!("{h:016x}"), texts });
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn joyride_worlds() {
        assert!(is_joyride_world("worlds_\\sp_\\joyride_\\world4"));
        assert!(is_joyride_world("WORLDS_/SP_/JOYRIDE_/world1"));
        assert!(!is_joyride_world("worlds_\\sp_\\chal1_\\ch1_l2"));
    }

    #[test]
    fn template_without_base() {
        let o = NewLevel {
            mode: "challenge".into(),
            world: "worlds_\\sp_\\joyride_\\world4".into(),
            extends: 39,
            name: "Canyon Dash".into(),
            dir: PathBuf::from("/nonexistent"),
            clean: false,
            base: None,
        };
        // a level on a Joyride world is made from its base (its script, its collectibles)
        assert!(template_files(&o).is_err());
        // on its mode's own world it needs none
        let o2 = NewLevel { world: "worlds_\\sp_\\chal1_\\ch1_l2".into(), ..o };
        let (text, trigs) = template_files(&o2).unwrap();
        assert!(text.contains("\"extends\": \"base://mcp/track/39\""));
        assert!(!text.contains("modeWorld"));
        assert!(trigs.is_empty());
        // the modes' screens
        for m in ["challenge", "mission", "battle", "drag"] {
            assert!(mode_screens(m).is_some());
        }
    }
}
