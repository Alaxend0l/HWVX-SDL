//! The viewer protocol end to end: runs the game in viewer mode, loads a car pack, reads its
//! outline, gets and sets a field, frames it, saves a screenshot, quits (and the game is killed).
//!
//! Needs a build of the game and GameCube data:
//!   HWVX_GAME   the hwvx program (default: build/hwvx of this checkout)
//!   HWVX_DATA   the extracted disc's files folder (the test is skipped without it)
//!   cargo test --manifest-path tools/browser/Cargo.toml -- --nocapture

use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use serde_json::{Value, json};
use std::path::PathBuf;

fn field<'a>(node: &'a Value, name: &str) -> &'a Value {
    node["fields"].as_array().and_then(|f| f.iter().find(|f| f["name"] == name)).map(|f| &f["value"]).unwrap_or(&Value::Null)
}

#[test]
fn car_pack_outline_set_screenshot() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let game = std::env::var_os("HWVX_GAME").map(PathBuf::from).unwrap_or_else(|| {
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/hwvx")
    });
    let game = std::fs::canonicalize(&game).unwrap_or_else(|_| default_game());
    let out = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("protocol");
    std::fs::create_dir_all(&out).unwrap();
    let mut v = Viewer::spawn(&SpawnOptions {
        schemas: default_schemas(&game),
        game,
        data: Some(data),
        pref: out.join("pref"),
        window: Some("640x480".into()),
        renderer: None,
        log_file: Some(out.join("viewer.log")),
    })
    .expect("the viewer starts");

    let hello = v.call("hello", json!({})).unwrap();
    assert_eq!(hello["protocol"], 1);
    assert!(hello["schemas"].as_array().unwrap().iter().any(|s| s == "CarAInfo"));

    let packs = v.call("packs", json!({})).unwrap();
    assert!(packs["packs"].as_array().unwrap().iter().any(|p| p["pack"] == "HW_Cars/car_5"));

    let load = v.call("load", json!({"archive": "HW_Cars", "type": "car", "id": 5})).unwrap();
    assert_eq!(load["pack"], "HW_Cars/car_5");
    assert!(load["numNodes"].as_u64().unwrap() > 100);

    let outline = v.call("outline", json!({"pack": "HW_Cars/car_5"})).unwrap();
    let nodes = outline["nodes"].as_array().unwrap();
    let engine = nodes.iter().find(|n| n["id"] == "CarAInfo/0/engine").expect("CarAInfo/0/engine in the outline");
    assert_eq!(engine["type"], "CarAEngine");
    assert_eq!(engine["parent"], "CarAInfo/0");
    assert!(nodes.iter().filter(|n| n["kind"] == "geometry").count() > 5);

    let info = v.call("get", json!({"node": "CarAInfo/0"})).unwrap();
    let rpm = field(&info, "maxRpm").as_f64().expect("maxRpm is a number");
    assert!(rpm > 1000.0);
    assert_eq!(field(&info, "engine"), "CarAInfo/0/engine");

    let set = v.call("set", json!({"node": "CarAInfo/0", "field": "maxRpm", "value": rpm + 1234.0})).unwrap();
    assert_eq!(set["value"].as_f64(), Some(rpm + 1234.0));
    let set = v.call("set", json!({"node": "CarAInfo/0", "field": "viewOffset/1", "value": 3.5})).unwrap();
    assert_eq!(set["value"].as_f64(), Some(3.5));
    let info = v.call("get", json!({"node": "CarAInfo/0"})).unwrap();
    assert_eq!(field(&info, "maxRpm").as_f64(), Some(rpm + 1234.0));
    assert_eq!(field(&info, "viewOffset")[1].as_f64(), Some(3.5));
    // what can't be set says why
    assert!(v.call("set", json!({"node": "CarAInfo/0", "field": "noSuchField", "value": 1})).unwrap_err().contains("no field"));
    // a reload is the files' values again
    v.call("load", json!({"archive": "HW_Cars", "type": "car", "id": 5})).unwrap();
    let info = v.call("get", json!({"node": "CarAInfo/0"})).unwrap();
    assert_eq!(field(&info, "maxRpm").as_f64(), Some(rpm));

    v.call("focus", json!({"node": null})).unwrap();
    let cam = v.call("orbit", json!({"dyaw": 30, "dpitch": 5})).unwrap();
    assert!(cam["camera"]["distance"].as_f64().unwrap() > 0.0);

    let shot = out.join("car5.bmp");
    let _ = std::fs::remove_file(&shot);
    let r = v.call("screenshot", json!({"path": shot.to_str().unwrap()})).unwrap();
    assert!(r["drawn"].as_u64().unwrap() > 0, "the car was drawn");
    let bytes = std::fs::read(&shot).expect("the screenshot was written");
    assert!(bytes.len() > 54 + 4 * 100 * 100);
    // not one colour: something is in the picture
    let px: std::collections::HashSet<&[u8]> = bytes[54..].chunks(4).take(400_000).collect();
    assert!(px.len() > 50, "the screenshot has {} colours", px.len());
    eprintln!("screenshot: {}", shot.display());

    v.call("quit", json!({})).unwrap();
    v.kill();
}
