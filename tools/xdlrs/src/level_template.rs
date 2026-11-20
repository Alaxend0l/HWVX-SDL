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
    let mut intro_end: Option<Value> = None;
    if let Some(base) = &o.base {
        let b = BaseWorld::open(base)?;
        if b.archive.to_lowercase() != archive {
            return Err(lerr(format!("the base given is {}, not the level's world {archive}", b.archive)));
        }
        // the world's intro screen: the interface its interface_end trigger waits for
        for (_, f) in b.trigger_files_pub() {
            let d = read_jsonc(&f)?;
            if d.get("type").and_then(Value::as_str) == Some("interface_end")
                && let Some(p) = d.get("param").filter(|p| p.get("interface").is_some())
            {
                intro_end = Some(p.clone());
                break;
            }
        }
        if o.clean {
            let c = collectibles(base)?;
            if !c.items.is_empty() || !c.triggers.is_empty() {
                lv.insert("baseTriggers".into(), json!(c.kept));
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
    } else if o.clean && template {
        return Err(lerr("removing the template's gears and key needs its base world (--data)"));
    }
    if template {
        let mut strings = Map::new();
        let mut vars = Map::new();
        let mut timers = Map::new();
        vars.insert("done".into(), json!(0));
        let brief = match o.mode.as_str() {
            "challenge" => "Beat the clock!",
            "mission" => "Your mission starts here",
            "battle" => "Battle: 5 kills win a round",
            _ => "Drag race: first to the finish",
        };
        if let Some(h) = hud {
            // the Joyride intro box shows the objectives with its objects' ids: the removed
            // ones' first id is free for the level's own line
            strings.insert("brief".into(), json!(brief));
            vars.insert("briefed".into(), json!(0));
            lv.insert(
                "objectives".into(),
                json!({"brief": {"text": "level://strings/brief", "kind": "none", "done": "level://variables/briefed/to", "hud": h}}),
            );
        }
        match o.mode.as_str() {
            "challenge" | "mission" => {
                // the HUD's clock (type 12, as retail's round timers), started when the intro ends
                timers.insert("clock".into(), json!({"time": 60000, "countUp": 0, "type": 12}));
                let go = json!([{"type": "timer_change_mode", "data": {"show": "show", "run": "resume", "timers": ["level://timers/clock"]}}]);
                match &intro_end {
                    Some(p) => trigs.push(("go".into(), trigger("interface_end", p.clone(), Value::Null, go))),
                    None => trigs.push(("go".into(), trigger("level_start", Value::Null, Value::Null, go))),
                }
                trigs.push((
                    "timeout".into(),
                    trigger(
                        "timer_value_check",
                        json!({"timers": ["level://timers/clock"], "value": {"const": 0}, "op": "equal"}),
                        json!([var_is("level://variables/done", 0)]),
                        json!([set_var("level://variables/done", 1), {"type": "end_level", "data": {"result": 0, "arg": 0}}]),
                    ),
                ));
                notes.push("add what wins it: a trigger with end_level result 4 (a volume to reach, items to pick up)".into());
            }
            _ => {
                // round rules, the HUD's round clock (type 12: the rules set its time), and the round
                // flow a battle or drag world's script has (results screens there): the next round
                // after 3 s; after the last, the level ends won if player 1 won the series
                let battle = o.mode == "battle";
                timers.insert("round".into(), json!({"time": if battle { 120000 } else { 0 }, "countUp": 0, "type": 12}));
                lv.insert(
                    "rules".into(),
                    if battle {
                        json!({"mode": "kills", "kills": 5, "rounds": 3, "time": 120000, "roundTimer": "level://timers/round"})
                    } else {
                        json!({"mode": "none", "rounds": 1, "time": 0})
                    },
                );
                if battle {
                    let go = json!([{"type": "timer_change_mode", "data": {"show": "show", "run": "resume", "timers": ["level://timers/round"]}}]);
                    match &intro_end {
                        Some(p) => trigs.push(("go".into(), trigger("interface_end", p.clone(), Value::Null, go))),
                        None => trigs.push(("go".into(), trigger("level_start", Value::Null, Value::Null, go))),
                    }
                }
                trigs.push((
                    "next-round".into(),
                    trigger(
                        "game_round_x_complete",
                        Value::Null,
                        Value::Null,
                        json!([
                            {"type": "vehicle_change_control", "data": {"mode": "lock_control", "unused": 0, "cars": [car0]}, "count": "always"},
                            {"type": "reset_game_round", "data": {"resetMotion": 1, "resetPosition": 1, "jettison": 1}, "delayInit": 3000, "count": "always"},
                            {"type": "vehicle_change_control", "data": {"mode": "unlock_control", "unused": 0, "cars": [car0]}, "delayInit": 3500, "count": "always"}
                        ]),
                    ),
                ));
                for (n, won) in [("won", true), ("lost", false)] {
                    trigs.push((
                        n.into(),
                        trigger(
                            "game_rounds_complete",
                            Value::Null,
                            json!([{"type": "code_variable_value_check", "param": {"op": "equal", "var": {"var": 404, "arg": 1}, "value": {"const": won as i64}}}]),
                            json!([
                                {"type": "vehicle_change_control", "data": {"mode": "lock_control", "unused": 0, "cars": [car0]}},
                                {"type": "end_level", "data": {"result": if won { 4 } else { 0 }, "arg": 0}, "delayInit": 2000}
                            ]),
                        ),
                    ));
                }
                notes.push(if battle {
                    "add a start for each opponent (port 0: an AI car; a battle needs 2 starts at least)".into()
                } else {
                    "add 3 starts, one per lane (4 in all), a finish volume and a round_complete trigger per car".into()
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
        let (text, trigs) = template_files(&o).unwrap();
        assert!(text.contains("\"extends\": \"base://mcp/track/39\""));
        assert!(text.contains("\"world\": \"worlds_\\\\sp_\\\\joyride_\\\\world4\""));
        assert_eq!(trigs.iter().map(|t| t.0.as_str()).collect::<Vec<_>>(), ["go", "timeout"]);
        // removing needs the base
        let o2 = NewLevel { clean: true, ..o.clone() };
        assert!(template_files(&o2).is_err());
        // a battle template: rules and the round flow
        let o3 = NewLevel { mode: "battle".into(), extends: 19, ..o };
        let (text, trigs) = template_files(&o3).unwrap();
        assert!(text.contains("\"roundTimer\": \"level://timers/round\""));
        assert_eq!(trigs.iter().map(|t| t.0.as_str()).collect::<Vec<_>>(), ["go", "next-round", "won", "lost"]);
    }
}
