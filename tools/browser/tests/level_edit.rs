//! The level editor (stage 21.3): the project model (src/level_model.rs: patches, undo and redo,
//! saving that keeps comments, the copy the level tools check) and the viewer's editor commands
//! (`records`, `record`, `level`, `marks`, `pick`'s path and ray) on ch1_l2.
//!
//!   HWVX_DATA=<GameCube files> cargo test --manifest-path tools/browser/Cargo.toml --test level_edit -- --test-threads=1

use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use hwvx_browser::level_model::Project;
use serde_json::{Value, json};
use std::path::{Path, PathBuf};

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..").canonicalize().unwrap()
}

fn scratch(name: &str) -> PathBuf {
    let d = root().join("build-browser-test").join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

fn copy_dir(from: &Path, to: &Path) {
    std::fs::create_dir_all(to).unwrap();
    for e in std::fs::read_dir(from).unwrap() {
        let e = e.unwrap();
        if e.file_type().unwrap().is_dir() {
            copy_dir(&e.path(), &to.join(e.file_name()));
        } else {
            std::fs::copy(e.path(), to.join(e.file_name())).unwrap();
        }
    }
}

/// The level tools' check (xdl_level.py check --json's result)
fn check(dir: &Path) -> Value {
    let env = xdlrs::level::Env::new(&root()).unwrap();
    let (e, w) = xdlrs::level::check(&env, dir, &Default::default(), None).unwrap();
    let rec = |x: &[String]| Value::Array(xdlrs::level::error_records(x).iter().map(|r| r.to_json()).collect());
    json!({"ok": e.is_empty(), "errors": rec(&e), "warnings": rec(&w)})
}

#[test]
fn model_edit_undo_save() {
    let s = scratch("model");
    let proj = s.join("project");
    copy_dir(&root().join("tools/level-samples/checkpoint-run"), &proj);
    let mut p = Project::load(&proj).unwrap();
    assert_eq!(p.triggers.len(), 7);
    assert_eq!(p.base_prefix(), "base://ch1_l2/world_0/");
    assert!(!p.dirty());

    // a patch on the start, a volume moved, a trigger's delay: three undo points
    p.push_undo();
    p.set_patch("World/0/tileCars/0", "pos/x", Some(json!(-300.0)));
    p.push_undo();
    p.level["volumes"]["cp2"]["center"] = json!([1300.0, 3.0, -730.0]);
    p.push_undo();
    p.trigger_mut("cp3").unwrap().value["actions"][3]["delayInit"] = json!(1500);
    assert!(p.dirty());
    assert_eq!(p.patch("World/0/tileCars/0", "pos/x"), Some(&json!(-300.0)));
    assert!(p.undo());
    assert_eq!(p.trigger("cp3").unwrap().value["actions"][3]["delayInit"], 1000);
    assert!(p.undo());
    assert_eq!(p.level["volumes"]["cp2"]["center"], json!([1252, 3, -730]));
    assert!(p.redo());
    assert!(p.redo());
    assert_eq!(p.trigger("cp3").unwrap().value["actions"][3]["delayInit"], 1500);
    // a patch taken out leaves nothing behind
    p.set_patch("World/0/timers/0/to", "time", None);
    assert!(p.level["patches"].get("base://ch1_l2/world_0/World/0/timers/0/to").is_none());
    p.set_patch("World/0/timers/0/to", "time", Some(json!(30000)));

    // the copy checks as the tool checks
    let copy = s.join("copy");
    p.write_copy(&copy).unwrap();
    assert_eq!(check(&copy)["ok"], true, "{}", check(&copy));

    // saved: the changed files, their comments kept
    let files = p.save().unwrap();
    assert_eq!(files.len(), 2, "{files:?}");
    let level = std::fs::read_to_string(proj.join("level.jsonc")).unwrap();
    assert!(level.contains("// the round timer: 40 seconds"), "{level}");
    assert!(level.contains("// checkpoints reached"));
    assert!(level.contains("\"pos/x\": -300"), "{level}");
    assert!(level.contains("[1300.0, 3.0, -730.0]") || level.contains("[1300, 3, -730]"), "{level}");
    let cp3 = std::fs::read_to_string(proj.join("triggers/cp3.jsonc")).unwrap();
    assert!(cp3.contains("// The last checkpoint"), "{cp3}");
    assert!(cp3.contains("\"delayInit\": 1500"), "{cp3}");
    assert!(!p.dirty());
    assert_eq!(check(&proj)["ok"], true);
    // what was saved reads back as it was edited
    let q = Project::load(&proj).unwrap();
    assert_eq!(q.level, p.level);
    assert_eq!(q.trigger("cp3").unwrap().value, p.trigger("cp3").unwrap().value);

    // records added and taken out (stage 21.4): a trigger, a start, then undone and saved
    p.push_undo();
    let n = p.free_name("triggers", "trigger");
    assert_eq!(n, "trigger1");
    p.add_trigger(&n, json!({"type": "level_start", "actions": [{"type": "hud_change_mode", "data": {"unused": 1, "on": 1}}]}));
    p.push_undo();
    p.level["starts"] = json!({"start1": {"pos": [-330.0, 3.6, -740.0], "rotY": 1.5708, "port": 2}});
    p.push_undo();
    p.remove_trigger("music");
    assert!(p.trigger("music").is_none());
    assert!(!p.level["triggers"].as_array().unwrap().iter().any(|x| x == "music"));
    assert!(p.undo());
    assert!(p.trigger("music").is_some(), "undo brings a removed trigger back");
    assert!(p.undo());
    assert!(p.undo());
    assert!(p.trigger("trigger1").is_none(), "undo takes an added trigger out");
    assert!(p.redo() && p.redo() && p.redo());
    assert!(p.trigger("trigger1").is_some() && p.trigger("music").is_none());
    p.write_copy(&copy).unwrap();
    assert_eq!(check(&copy)["ok"], true, "{}", check(&copy));
    p.save().unwrap();
    assert!(proj.join("triggers/trigger1.jsonc").exists());
    assert!(!proj.join("triggers/music.jsonc").exists(), "a removed trigger's file is deleted on save");
    let q = Project::load(&proj).unwrap();
    assert_eq!(q.level["starts"]["start1"]["port"], 2);

    // a broken link: the check names the file and place
    p.trigger_mut("cp1").unwrap().value["param"]["volumes"][0] = json!("level://volumes/nope");
    p.write_copy(&copy).unwrap();
    let r = check(&copy);
    assert_eq!(r["ok"], false);
    assert!(r["errors"].as_array().unwrap().iter().any(|e| e["file"] == "triggers/cp1.jsonc"), "{r}");
}

#[test]
fn viewer_editor_commands() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let game = std::env::var_os("HWVX_GAME").map(PathBuf::from).unwrap_or_else(default_game);
    let s = scratch("viewer-edit");
    let mut v = Viewer::spawn(&SpawnOptions {
        schemas: default_schemas(&game),
        game,
        data: Some(data),
        pref: s.join("pref"),
        window: Some("320x240".into()),
        renderer: std::env::var("HWVX_RENDERER").ok(),
        log_file: Some(s.join("viewer.log")),
        token: None,
        viewer_window: false,
    })
    .expect("the viewer starts");
    assert!(v.call("records", json!({"table": "tileCars"})).is_err(), "no world: an error");
    v.call("world", json!({"name": "ch1_l2"})).unwrap();

    let r = v.call("records", json!({"table": "tileCars"})).unwrap();
    let cars = r["records"].as_array().unwrap();
    assert_eq!(cars.len(), 1);
    assert_eq!(cars[0]["path"], "World/0/tileCars/0");
    assert_eq!(cars[0]["port"], 1);
    let items = v.call("records", json!({"table": "items"})).unwrap();
    assert!(items["records"].as_array().unwrap().iter().any(|e| e["path"].as_str().unwrap().contains("/contents/items/")));
    assert!(v.call("records", json!({"table": "nope"})).is_err());

    let rec = v.call("record", json!({"path": "World/0/tileCars/0"})).unwrap();
    assert_eq!(rec["struct"], "TileCar");
    assert!((rec["fields"]["pos"]["x"].as_f64().unwrap() + 364.6).abs() < 0.1);
    let t = v.call("record", json!({"path": "World/0/timers/0/to"})).unwrap();
    assert_eq!(t["struct"], "WorldTimer");
    assert_eq!(t["fields"]["time"], 50000.0);
    assert!(v.call("record", json!({"path": "World/0/tileCars/5"})).is_err(), "past the table");

    // the live preview: a level applied to the world on show, its patches in place
    let proj = s.join("project");
    copy_dir(&root().join("tools/level-samples/checkpoint-run"), &proj);
    let mut p = Project::load(&proj).unwrap();
    p.set_patch("World/0/tileCars/0", "pos/x", Some(json!(-200.0)));
    p.set_patch("World/0/tileCars/0", "rotY", Some(json!(0.5)));
    let copy = s.join("copy");
    p.write_copy(&copy).unwrap();
    let out = s.join("preview.json");
    let env = xdlrs::level::Env::new(&root()).unwrap();
    let c = xdlrs::level::compile(&env, &copy, Some(&out), &Default::default(), false, None).unwrap();
    assert!(c.ok, "{:?}", c.errors);
    let a = v.call("level", json!({"path": out, "overlay": false})).unwrap();
    assert_eq!(a["level"], "uid://checkpointrun");
    assert_eq!(v.call("record", json!({"path": "World/0/timers/0/to"})).unwrap()["fields"]["time"], 40000.0);
    let rec = v.call("record", json!({"path": "World/0/tileCars/0"})).unwrap();
    assert_eq!(rec["fields"]["pos"]["x"], -200.0);
    assert_eq!(rec["fields"]["rotY"], 0.5);
    let recs = v.call("records", json!({"table": "tileCars"})).unwrap();
    assert_eq!(recs["records"][0]["pos"][0], -200.0, "records see the preview too");
    // applied again (an edit later): still one level, its values
    v.call("level", json!({"path": out})).unwrap();
    assert_eq!(v.call("record", json!({"path": "World/0/tileCars/0"})).unwrap()["fields"]["pos"]["x"], -200.0);
    // taken out: the base's values back
    let n = v.call("level", json!({"path": null})).unwrap();
    assert!(n["level"].is_null());
    assert_eq!(v.call("record", json!({"path": "World/0/timers/0/to"})).unwrap()["fields"]["time"], 50000.0);
    assert!((v.call("record", json!({"path": "World/0/tileCars/0"})).unwrap()["fields"]["pos"]["x"].as_f64().unwrap() + 364.6).abs() < 0.1);
    assert!(v.call("level", json!({"path": "/nonexistent/level.json"})).is_err());

    // patches of every kind a base record's fields have: an enum by name, a number array, a
    // quaternion's member (an effect point's), a byte; applied after the level was taken out
    let item = items["records"][0]["path"].as_str().unwrap().to_string();
    let fx = v.call("records", json!({"table": "fxPoints"})).unwrap()["records"][0]["path"].as_str().unwrap().to_string();
    let compile = |p: &Project, name: &str| {
        let copy = s.join(format!("copy-{name}"));
        let _ = std::fs::remove_dir_all(&copy);
        p.write_copy(&copy).unwrap();
        let out = s.join(format!("{name}.json"));
        (xdlrs::level::compile(&env, &copy, Some(&out), &Default::default(), false, None).unwrap(), out)
    };
    let mut p = Project::load(&proj).unwrap();
    p.set_patch(&item, "mode", Some(json!("off")));
    p.set_patch(&item, "unk_0x2C", Some(json!([1, 2, 3, 4])));
    p.set_patch(&item, "aiPickup", Some(json!(0)));
    p.set_patch(&fx, "quat/y", Some(json!(0.7071)));
    p.set_patch(&fx, "quat/w", Some(json!(0.7071)));
    let (c, out) = compile(&p, "kinds");
    assert!(c.ok, "{:?}", c.errors);
    v.call("level", json!({"path": out})).unwrap();
    let r = v.call("record", json!({"path": item})).unwrap();
    assert_eq!((r["fields"]["mode"].clone(), r["fields"]["unk_0x2C"].clone(), r["fields"]["aiPickup"].clone()), (json!(0), json!([1, 2, 3, 4]), json!(0)), "{r}");
    let q = &v.call("record", json!({"path": fx})).unwrap()["fields"]["quat"];
    assert!((q["y"].as_f64().unwrap() - 0.7071).abs() < 1e-4 && (q["w"].as_f64().unwrap() - 0.7071).abs() < 1e-4, "{q}");
    v.call("level", json!({"path": null})).unwrap();
    assert_eq!(v.call("record", json!({"path": item})).unwrap()["fields"]["mode"], 1, "taken out");
    v.call("level", json!({"path": out})).expect("applied again after it was taken out");
    v.call("level", json!({"path": null})).unwrap();

    // what patches can't set, said at the field by the compiler: a link, a list of links, a
    // number the game indexes with
    for (path, field, val, why) in [
        (item.as_str(), "record", json!("base://ch1_l2/world_0/World/0/tileCars/0"), "a link"),
        ("World/0/tileCars/0", "scripts", json!([0, 0, 0, 0]), "a link"),
        ("World/0/timers/0/to", "countUp", json!(1), "indexes with"),
        ("World/0/tileCars/0", "port", json!(2), "indexes with"),
        ("World/0/volumes/0", "type", json!("fog"), "indexes with"),
    ] {
        let mut p = Project::load(&proj).unwrap();
        p.set_patch(path, field, Some(val));
        let (c, _) = compile(&p, "refused");
        assert!(!c.ok);
        assert!(c.errors.iter().any(|e| e.path.ends_with(&format!("{path}/{field}")) || e.message.contains(&format!("{field}: "))), "{path} {field}: {:?}", c.errors);
        assert!(c.errors.iter().any(|e| e.message.contains(why)), "{path} {field}: {:?}", c.errors);
    }
    // ... and by the game, naming the patch's record and field (a level file the compiler
    // didn't make: the timer's countUp byte)
    let mut p = Project::load(&proj).unwrap();
    p.set_patch("World/0/timers/0/to", "time", Some(json!(30000)));
    let (c, out) = compile(&p, "crafted");
    assert!(c.ok);
    let mut lv: Value = serde_json::from_str(&std::fs::read_to_string(&out).unwrap()).unwrap();
    let pt = lv["patches"].as_array_mut().unwrap().iter_mut().find(|x| x["field"] == "time").unwrap();
    pt["offset"] = json!(3);
    pt["hex"] = json!("01");
    std::fs::write(&out, lv.to_string()).unwrap();
    let e = v.call("level", json!({"path": out})).unwrap_err();
    assert!(e.contains("(World/0/timers/0/to time): over a number the game indexes with"), "{e}");

    // the starts' box: every car's collision box at once (retail: x -5..5, y -1.4..5.25, z -14..13)
    let bf = s.join("spawn-box.json");
    let b = v.call("spawnbox", json!({"path": bf})).unwrap();
    assert_eq!(b["box"]["cars"], 33);
    let near = |v: &Value, w: [f64; 3]| (0..3).all(|i| (v[i].as_f64().unwrap() - w[i]).abs() < 0.01);
    assert!(near(&b["box"]["min"], [-5.0, -1.4, -14.0]) && near(&b["box"]["max"], [5.0, 5.25, 13.0]), "{b}");
    assert!(std::fs::read_to_string(&bf).unwrap().contains("\"cars\": 33"));
    // a start against the world: the retail start (its origin on the road, the box 1.4 below it) is
    // clear; too low (0.5 deeper), too high, inside the world not
    let sc = v.call("startcheck", json!({"starts": [
        {"pos": [-364.6, 2.22, -780.6], "rotY": 1.5708},
        {"pos": [-364.6, -1.0, -780.6], "rotY": 1.5708},
        {"pos": [-364.6, 20.0, -780.6], "rotY": 1.5708},
        {"pos": [-300.0, 3.61, -700.0], "rotY": 1.5708}
    ]})).unwrap();
    let st = sc["starts"].as_array().unwrap();
    assert_eq!(st[0]["clear"], true, "{sc}");
    assert!((st[0]["seat"].as_f64().unwrap() - 2.21).abs() < 0.1, "the seat: the car's origin on the road: {sc}");
    let low = v.call("startcheck", json!({"starts": [{"pos": [-364.6, 1.72, -780.6], "rotY": 1.5708}]})).unwrap();
    assert!(low["starts"][0]["problems"].as_str().unwrap().contains("into the ground"), "0.5 lower: {low}");
    assert!(st[1]["problems"].as_str().unwrap().contains("into the ground"), "{sc}");
    assert!(st[2]["problems"].as_str().unwrap().contains("above the ground"), "{sc}");
    assert!(st[3]["problems"].as_str().unwrap().contains("wall"), "{sc}");

    // marks
    let m = v.call("marks", json!({"marks": [
        {"kind": "sphere", "pos": [82, 4, -780], "radius": 90},
        {"kind": "arrow", "pos": [-365, 2, -781], "rotY": 1.57, "color": [255, 255, 0]},
        {"kind": "line", "pos": [0, 0, 0], "to": [0, 10, 0]},
        {"kind": "start", "pos": [-365, 3.6, -781], "rotY": 1.57}
    ]})).unwrap();
    assert_eq!(m["marks"], 4);
    assert!(v.call("marks", json!({"marks": [{"kind": "line", "pos": [0, 0, 0]}]})).is_err(), "a line needs to");
    v.call("marks", json!({"marks": []})).unwrap();

    // a pick at the start: the tile car's path, and the ray
    v.call("frames", json!({"width": 400, "height": 300})).unwrap();
    v.call("goto", json!({})).unwrap();
    v.call("camera", json!({"pitch": 89, "distance": 40})).unwrap();
    v.call("status", json!({})).unwrap();
    let pk = v.call("pick", json!({"x": 200, "y": 150})).unwrap();
    assert_eq!(pk["origin"].as_array().map(|a| a.len()), Some(3), "{pk}");
    assert_eq!(pk["dir"].as_array().map(|a| a.len()), Some(3));
    assert_eq!(pk["path"], "World/0/tileCars/0", "{pk}");
    // a drag's ground point: the ray through the middle of the picture meets the road at the start
    let g = v.call("raycast", json!({"x": 200, "y": 150})).unwrap();
    assert_eq!(g["hit"], true, "{g}");
    assert!((g["point"][0].as_f64().unwrap() + 365.0).abs() < 20.0 && (g["point"][2].as_f64().unwrap() + 781.0).abs() < 20.0, "{g}");

    // the gizmo: the camera's view as the viewer reports it gives the rays pick makes, and puts
    // the point a pick's ray hits back under the pixel (an angled view, off the picture's middle)
    v.call("camera", json!({"pitch": 30, "yaw": 40, "distance": 120})).unwrap();
    let st = v.call("status", json!({})).unwrap();
    let view = hwvx_browser::gizmo::View::from_camera(&st["camera"]).expect("status: camera.view");
    assert_eq!(view.size, [400.0, 300.0], "{st}");
    for (x, y) in [(200.0f32, 150.0f32), (37.0, 260.0), (390.0, 12.0)] {
        let pk = v.call("pick", json!({"x": x, "y": y})).unwrap();
        let (o, d) = view.ray(x, y);
        for i in 0..3 {
            assert!((pk["origin"][i].as_f64().unwrap() as f32 - o[i]).abs() < 0.01, "origin {pk} {o:?}");
            assert!((pk["dir"][i].as_f64().unwrap() as f32 - d[i]).abs() < 1e-4, "dir {pk} {d:?}");
        }
        let at = view.project(hwvx_browser::gizmo::add(o, d, 80.0)).unwrap();
        assert!((at[0] - x).abs() < 0.01 && (at[1] - y).abs() < 0.01, "{at:?} for {x}, {y}");
    }
    v.call("quit", json!({})).ok();
}
