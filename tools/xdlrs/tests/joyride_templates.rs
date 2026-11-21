//! Joyride worlds as templates (xdlrs::level_template, the level compiler's `remove`).
//!
//! Without game data: the compiler's checks of `remove` on a project of its own. With the
//! GameCube data and hwvx-x where this machine has them (XDLRS_GC_FILES, XDLRS_HWVX_X; skipped
//! otherwise): on each of the five Joyride worlds the collectibles are found (ten gears, a key,
//! two objectives, six triggers), and a level of every mode made on it compiles, the collectibles
//! removed (13 removes and the template's triggers) or kept (their triggers with them).

use serde_json::Value;
use std::path::{Path, PathBuf};
use xdlrs::level::{self, BaseOpts, Env};
use xdlrs::level_template::{self, NewLevel};

fn place(var: &str, default: &str) -> Option<PathBuf> {
    let p = PathBuf::from(std::env::var(var).unwrap_or_else(|_| default.to_string()));
    p.exists().then_some(p)
}

fn env() -> Env {
    let root = xdlrs::repo_root(None).expect("the checkout (schemas/)");
    let mut e = Env::new(&root).unwrap();
    e.hwvx_x = place("XDLRS_HWVX_X", "/home/alaxendol/Develop/HWVX-X-FILE-EDITOR/target/release/hwvx-x");
    e
}

fn scratch(name: &str) -> PathBuf {
    let d = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

fn write(p: &Path, text: &str) {
    std::fs::create_dir_all(p.parent().unwrap()).unwrap();
    std::fs::write(p, text).unwrap();
}

#[test]
fn remove_is_checked() {
    let env = env();
    let d = scratch("remove-checked");
    let lv = r#"{"format_version": 1, "id": "uid://rm", "name": "Rm", "mode": "challenge",
        "extends": "base://mcp/track/39", "world": "worlds_\\sp_\\joyride_\\world4",
        "remove": {"items": ["base://world4/world_0/World/0/quads/3/contents/items/0",
                             "base://world4/world_0/World/0/quads/3/contents/items/0",
                             "base://world1/world_0/World/0/quads/3/contents/items/0",
                             "base://world4/world_0/World/0/tileCars/0"],
                   "objectives": ["base://world4/world_0/World/0/objectives/1"],
                   "volumes": []},
        "patches": {"base://world4/world_0/World/0/objectives/1": {"arg": 1}}}"#;
    write(&d.join("level.jsonc"), lv);
    std::fs::create_dir_all(d.join("triggers")).unwrap();
    let (errors, _) = level::check(&env, &d, &BaseOpts { base: None, data: None, cache: None }, None).unwrap();
    let has = |s: &str| errors.iter().any(|e| e.contains(s));
    assert!(has("remove/items/1: base://world4/world_0/World/0/quads/3/contents/items/0: removed twice"), "{errors:?}");
    assert!(has("remove/items/2: "), "{errors:?}");
    assert!(has("remove/items/3: "), "{errors:?}");
    assert!(has("remove/objectives/0: base://world4/world_0/World/0/objectives/1: removed and patched"), "{errors:?}");
    assert!(has("remove/volumes: not a kind of record a level removes"), "{errors:?}");

    // a good one compiles to removes: a walk to the element, its kind and path
    let lv = r#"{"format_version": 1, "id": "uid://rm", "name": "Rm", "mode": "challenge",
        "extends": "base://mcp/track/39", "world": "worlds_\\sp_\\joyride_\\world4",
        "remove": {"items": ["base://world4/world_0/World/0/quads/3/contents/items/0"],
                   "objectives": ["base://world4/world_0/World/0/objectives/1"]}}"#;
    write(&d.join("level.jsonc"), lv);
    let c = level::compile(&env, &d, None, &BaseOpts { base: None, data: None, cache: None }, false, None).unwrap();
    assert!(c.ok);
    let rm = c.doc.as_ref().unwrap()["removes"].as_array().unwrap().clone();
    assert_eq!(rm.len(), 2);
    assert_eq!(rm[0]["kind"], "item");
    assert_eq!(rm[0]["path"], "World/0/quads/3/contents/items/0");
    assert_eq!(rm[1]["kind"], "objective");
    assert_eq!(rm[1]["target"]["root"], "world");
}

#[test]
fn ai_start_ports() {
    let mut f = serde_json::Map::new();
    f.insert("port".into(), Value::from(0));
    level::ai_start(&mut f).unwrap();
    assert_eq!(f["port"], 6);
    assert_eq!(f["dynamic"], 0);
    let mut f = serde_json::Map::new();
    f.insert("port".into(), Value::from(1));
    level::ai_start(&mut f).unwrap();
    assert_eq!(f["port"], 1);
    assert!(f.get("dynamic").is_none());
}

#[test]
fn every_mode_on_every_joyride_world() {
    let (Some(data), Some(_)) = (
        place("XDLRS_GC_FILES", "/home/alaxendol/Develop/HWVX-X-FILE-EDITOR/orig/gc/HWVX GC RIP/files"),
        place("XDLRS_HWVX_X", "/home/alaxendol/Develop/HWVX-X-FILE-EDITOR/target/release/hwvx-x"),
    ) else {
        eprintln!("skipped: no GameCube data or hwvx-x here");
        return;
    };
    let env = env();
    let cache = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("joyride-base-cache");
    for w in 1..=5 {
        let world = format!("worlds_\\sp_\\joyride_\\world{w}");
        let base = level::base_project(&env, &world, &data, &cache).unwrap();
        let c = level_template::collectibles(&base).unwrap();
        assert_eq!(c.items.len(), 11, "world{w}: ten gears and a key");
        assert_eq!(c.objectives.len(), 2, "world{w}");
        assert_eq!(c.triggers.len(), 6, "world{w}");
        assert_eq!(level_template::objective_hud_ids(&base, &c.objectives).unwrap(), [2, 1], "world{w}");
        for mode in ["challenge", "mission", "battle", "drag"] {
            // collectibles removed (the New level box ticked) and kept (unticked): both compile
            for clean in [true, false] {
                let dir = scratch(&format!("tpl-{mode}-world{w}-{}", if clean { "clean" } else { "kept" }));
                std::fs::remove_dir_all(&dir).unwrap();
                let o = NewLevel {
                    mode: mode.into(),
                    world: world.clone(),
                    extends: level_template::first_track(if mode == "mission" { "adventure" } else { mode }).unwrap(),
                    name: format!("{mode} {w}"),
                    dir: dir.clone(),
                    clean,
                    base: Some(base.clone()),
                };
                level_template::new_level(&o).unwrap();
                // never over a level that's there
                assert!(level_template::new_level(&o).is_err());
                let b = BaseOpts { base: Some(base.clone()), data: None, cache: None };
                let r = level::compile(&env, &dir, None, &b, false, None).unwrap();
                assert!(r.ok, "{mode} on world{w} (clean {clean}): {:?}", r.errors.iter().map(|e| e.message.clone()).collect::<Vec<_>>());
                let doc = r.doc.unwrap();
                let removes = doc.get("removes").and_then(Value::as_array).map_or(0, Vec::len);
                assert_eq!(removes, if clean { 13 } else { 0 }, "{mode} on world{w} (clean {clean})");
                // the Joyride intro (an interface and what waits for it) goes too: the mode's own screens
                let kept: Vec<i64> = doc["baseTriggers"].as_array().unwrap().iter().filter_map(Value::as_i64).collect();
                if clean {
                    assert!(kept.len() < c.kept.len() && kept.iter().all(|k| c.kept.contains(k)), "{mode} on world{w}: {kept:?}");
                } else {
                    assert!(c.triggers.iter().all(|t| kept.contains(t)), "{mode} on world{w}: {kept:?}");
                }
                assert_eq!(doc["modeWorld"], level_template::mode_screens(mode).unwrap().world);
                // (compile gives warnings with a spawn box only: the check's)
                let (errs, warnings) = level::check(&env, &dir, &b, None).unwrap();
                assert!(errs.is_empty(), "{errs:?}");
                let warned = |s: &str| warnings.iter().any(|x| x.contains(s));
                match mode {
                    // nothing wins it yet (the template leaves the goal to the level)
                    "challenge" | "mission" => assert!(warned("nothing wins this"), "{mode} on world{w}: {warnings:?}"),
                    // one start: a battle wants 2, a drag race 4
                    _ => assert!(warned(if mode == "battle" { "2 starts" } else { "4 lanes" }), "{mode} on world{w}: {warnings:?}"),
                }
                if clean {
                    assert!(!warned("still in it"), "{mode} on world{w}: the collectibles removed");
                }
            }
        }
    }
}
