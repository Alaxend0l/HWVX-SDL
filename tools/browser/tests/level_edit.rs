//! The level editor (stage 21.3): the project model (src/level_model.rs: patches, undo and redo,
//! saving that keeps comments, the copy xdl_level.py checks) and the viewer's editor commands
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

fn check(dir: &Path) -> Value {
    let out = std::process::Command::new("python3")
        .arg(root().join("tools/xdl_level.py"))
        .args(["check", "--json"])
        .arg(dir)
        .output()
        .unwrap();
    serde_json::from_slice(String::from_utf8_lossy(&out.stdout).lines().last().unwrap_or("{}").as_bytes()).unwrap()
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
    let st = std::process::Command::new("python3")
        .arg(root().join("tools/xdl_level.py"))
        .args(["compile", "--json", "-o"])
        .arg(&out)
        .arg(&copy)
        .status()
        .unwrap();
    assert!(st.success());
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

    // marks
    let m = v.call("marks", json!({"marks": [
        {"kind": "sphere", "pos": [82, 4, -780], "radius": 90},
        {"kind": "arrow", "pos": [-365, 2, -781], "rotY": 1.57, "color": [255, 255, 0]},
        {"kind": "line", "pos": [0, 0, 0], "to": [0, 10, 0]}
    ]})).unwrap();
    assert_eq!(m["marks"], 3);
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
    v.call("quit", json!({})).ok();
}
