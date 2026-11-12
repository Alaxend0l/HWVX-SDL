//! The viewer protocol end to end: runs the game in viewer mode, loads a car pack, reads its
//! outline, gets and sets a field, frames it, saves a screenshot, quits (and the game is killed).
//!
//! Needs a build of the game and GameCube data:
//!   HWVX_GAME   the hwvx program (default: build/hwvx of this checkout)
//!   HWVX_DATA   the extracted disc's files folder (the test is skipped without it)
//!   cargo test --manifest-path tools/browser/Cargo.toml -- --nocapture

use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use serde_json::{Value, json};
use std::io::{BufRead, BufReader, Read, Write};
use std::net::TcpStream;
use std::path::PathBuf;
use std::time::Duration;

/// A raw connection that sends `text` and returns what came back before the viewer closed it.
fn raw_exchange(port: u16, text: &str) -> String {
    let mut s = TcpStream::connect(("127.0.0.1", port)).unwrap();
    s.set_read_timeout(Some(Duration::from_secs(15))).unwrap();
    s.write_all(text.as_bytes()).unwrap();
    let mut out = String::new();
    // Err (a timeout) would mean the viewer kept the connection open
    s.read_to_string(&mut out).expect("the viewer closed the connection");
    out
}

fn first_error(reply: &str) -> String {
    let line = BufReader::new(reply.as_bytes()).lines().next().and_then(Result::ok).unwrap_or_default();
    let v: Value = serde_json::from_str(&line).unwrap_or(Value::Null);
    assert_eq!(v["ok"], false, "{reply}");
    v["error"].as_str().unwrap_or("").to_string()
}

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
    let v = Viewer::spawn(&SpawnOptions {
        schemas: default_schemas(&game),
        game,
        data: Some(data),
        pref: out.join("pref"),
        window: Some("640x480".into()),
        renderer: None,
        log_file: Some(out.join("viewer.log")),
        token: Some("test-token-1234".into()),
    })
    .expect("the viewer starts");
    let port = v.port;

    // only a client with the token is served; a web page's POST is not one (each of these
    // connections displaces the one before: the test goes on over a new one)
    assert_eq!(raw_exchange(port, "POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 16\r\n\r\n{\"cmd\":\"quit\"}\n"), "");
    assert!(first_error(&raw_exchange(port, "{\"cmd\":\"quit\"}\n")).contains("hello"));
    assert!(first_error(&raw_exchange(port, "{\"cmd\":\"hello\"}\n{\"cmd\":\"quit\"}\n")).contains("token"));
    assert!(first_error(&raw_exchange(port, "{\"cmd\":\"hello\",\"token\":\"test-token-123\"}\n")).contains("token"));
    let owner = v; // (kills the game when dropped)
    let mut v = Viewer::connect(port, "test-token-1234").expect("the right token is served");

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
    let set_err = |v: &mut Viewer, field: &str, value: Value| {
        v.call("set", json!({"node": "CarAInfo/0", "field": field, "value": value})).expect_err(field)
    };
    // raw offsets that wrap, or don't fit in 32 bits, are outside the node
    for f in ["@FFFFFFFE:u32", "@FFFFFFFF:u8", "@100000004:u32", "@:u32", "@4x:u32"] {
        assert!(set_err(&mut v, f, json!(0)).contains("outside"), "{f}");
    }
    // array indices: 0 .. n-1, nothing that wraps or goes negative
    for f in ["viewOffset/3", "viewOffset/4294967297", "viewOffset/-1", "viewOffset/1x"] {
        assert!(set_err(&mut v, f, json!(0)).contains("index"), "{f}");
    }
    // a store over any part of a pointer slot is refused
    let engine_at = info["fields"].as_array().unwrap().iter().find(|f| f["name"] == "engine").unwrap()["offset"].as_u64().unwrap();
    assert!(engine_at >= 4);
    for f in [format!("@{:x}:u32", engine_at - 2), format!("@{:x}:u16", engine_at + 2), format!("@{:x}:f64", engine_at - 4)] {
        assert!(set_err(&mut v, &f, json!(0)).contains("pointer slot"), "{f}");
    }
    // an object link takes a node of its target's type; a raw offset needs force
    assert!(set_err(&mut v, "engine", json!("CarAInfo/0")).contains("not a CarAEngine"));
    assert!(set_err(&mut v, "engine", json!("@0")).contains("force"));
    assert_eq!(field(&v.call("get", json!({"node": "CarAInfo/0"})).unwrap(), "engine"), "CarAInfo/0/engine");
    // a pack that isn't there leaves the one shown loaded
    assert!(v.call("load", json!({"archive": "HW_Cars", "type": "car", "id": 9999})).unwrap_err().contains("no car pack"));
    let loaded = v.call("loaded", json!({})).unwrap();
    assert!(loaded["packs"].as_array().unwrap().iter().any(|p| p["pack"] == "HW_Cars/car_5" && p["shown"] == true));
    // the background: numbers, clamped
    assert!(v.call("background", json!({"color": ["a", 0, 0]})).is_err());
    v.call("background", json!({"color": [300, -5, 64]})).unwrap();
    // screenshots: .bmp or .png, never over a file that isn't an image
    assert!(v.call("screenshot", json!({"path": out.join("shot.txt").to_str().unwrap()})).unwrap_err().contains(".bmp"));
    let precious = out.join("precious.bmp");
    std::fs::write(&precious, "not an image").unwrap();
    assert!(v.call("screenshot", json!({"path": precious.to_str().unwrap()})).unwrap_err().contains("isn't an image"));
    assert_eq!(std::fs::read_to_string(&precious).unwrap(), "not an image");
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
    drop(owner);
}
