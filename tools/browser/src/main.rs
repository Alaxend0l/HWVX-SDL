//! hwvx-browser: browse (and edit, live) the game's data packs, with the game itself as the
//! preview. It runs HWVX-SDL in viewer mode (HWVX_VIEWER, src/port/viewer.c) and talks to it over
//! the viewer protocol (docs/viewer-protocol.md). The preview is the game's picture, drawn in this
//! window's middle: the game renders it hidden, at the panel's size, and shares each frame through
//! shared memory (src/frames.rs). Drag to orbit, right/middle (or shift) drag to pan, wheel to
//! zoom, click to pick; over the picture F frames, Home resets, Space spins, arrows orbit, +/- zoom.
//! The Worlds tab loads a whole level (its quadrants streaming around the camera, src/world_ui.rs):
//! free fly with WASD (Q/E down/up, shift faster, wheel for the speed), G goes to the start.
//!
//!   hwvx-browser [--game build/hwvx] [--data DIR] [--schemas DIR] [--pref DIR] [--viewer-window]
//!                [--port N [--token T]]          a viewer already running (T: its token, or
//!                                                HWVX_VIEWER_TOKEN; it logs one it made up)
//!                [--load ARCHIVE:TYPE:ID] [--select NODE] [--app-shot FILE.bmp]
//!                [--world NAME [--overlays regions,loaded,...] [--view noclip,allquads,nofog]]
//!                                                a world instead of --load (--view: the 3D view's switches)
//!                [--level PROJECT_DIR] [--test-drive] [--quick-start] [--car N] [--tools DIR]
//!                [--testdrive-renderer R] [--level-select trigger:NAME|own:KIND/NAME|base:PATH]
//!   hwvx-browser --level PROJECT_DIR [--check] [--drive SECS] [--save-mod OUT.xmod] [--data DIR]
//!                                                headless: the level's check, a test drive (until
//!                                                it drives, at most SECS), Save as mod; exit 0: ok
//!
//! Runtime files (schemas/, the game, hwvx-x) are found as src/resources.rs says: packaged
//! beside the program, $HWVX_EDITOR_RESOURCES, or the checkout.
//!
//! The Level tab opens a level project (xdlrs::level); Test drive (F5) compiles it and
//! starts the game straight into it (src/level_ui.rs, src/testdrive.rs).
//!
//! --viewer-window shows the game's own window instead (the old separate preview, for debugging).
//! --app-shot saves this window (once the --load and --select replies are in) and quits:
//! for scripted screenshots of the browser itself. It gives up (exit 1) on an error reply, a
//! lost viewer, or after two minutes.

mod level_edit;
mod level_ui;
mod media_ui;
mod world_ui;

use eframe::egui;
use hwvx_browser::media::MediaReader;
use media_ui::{MediaState, View, media_kind};
use world_ui::WorldState;
use level_ui::LevelState;
use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use hwvx_browser::frames::{Frame, FrameReader};
use serde_json::{Value, json};
use std::collections::{HashMap, HashSet};
use std::path::PathBuf;
use std::sync::mpsc::{Receiver, TryRecvError};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

/// How long a request may wait for its reply (a big pack's load is a few seconds).
const REPLY_TIMEOUT: Duration = Duration::from_secs(60);
const APP_SHOT_TIMEOUT: Duration = Duration::from_secs(120);

#[derive(Default, Clone)]
struct Args {
    game: Option<PathBuf>,
    data: Option<PathBuf>,
    schemas: Option<PathBuf>,
    pref: Option<PathBuf>,
    port: Option<u16>,
    token: Option<String>,
    window: Option<String>,
    load: Option<(String, String, u64)>,
    world: Option<String>,
    overlays: Option<String>,
    view: Option<String>,
    select: Option<String>,
    app_shot: Option<PathBuf>,
    viewer_window: bool,
    /// tools/ of the checkout (its parent's schemas/ are what the level tools read); default:
    /// beside the game's
    tools: Option<PathBuf>,
    /// a level project to open (the Level tab)
    level: Option<String>,
    /// HWVX_RENDERER for test drives
    testdrive_renderer: Option<String>,
    /// start a test drive at once (scripts)
    test_drive: bool,
    quick_start: bool,
    car: Option<u32>,
    /// the level editor's selection at start: trigger:NAME, own:KIND/NAME, base:PATH, settings
    level_select: Option<String>,
    /// headless (no window): --check, --drive SECS, --save-mod OUT.xmod, on --level
    check: bool,
    drive: Option<u64>,
    save_mod: Option<PathBuf>,
}

fn parse_args() -> Result<Args, String> {
    let mut a = Args::default();
    let mut it = std::env::args().skip(1);
    while let Some(k) = it.next() {
        let mut val = || it.next().ok_or_else(|| format!("{k} needs a value"));
        match k.as_str() {
            "--game" => a.game = Some(val()?.into()),
            "--data" => a.data = Some(val()?.into()),
            "--schemas" => a.schemas = Some(val()?.into()),
            "--pref" => a.pref = Some(val()?.into()),
            "--port" => a.port = Some(val()?.parse().map_err(|_| "--port N")?),
            "--token" => a.token = Some(val()?),
            "--window" => a.window = Some(val()?),
            "--load" => {
                let v = val()?;
                let p: Vec<&str> = v.rsplitn(3, ':').collect();
                if p.len() != 3 {
                    return Err("--load ARCHIVE:TYPE:ID".into());
                }
                a.load = Some((p[2].into(), p[1].into(), p[0].parse().map_err(|_| "--load: ID is a number")?));
            }
            "--select" => a.select = Some(val()?),
            "--world" => a.world = Some(val()?),
            "--overlays" => a.overlays = Some(val()?),
            "--view" => a.view = Some(val()?),
            "--app-shot" => a.app_shot = Some(val()?.into()),
            "--viewer-window" => a.viewer_window = true,
            "--tools" => a.tools = Some(val()?.into()),
            "--level" => a.level = Some(val()?),
            "--testdrive-renderer" => a.testdrive_renderer = Some(val()?),
            "--test-drive" => a.test_drive = true,
            "--quick-start" => a.quick_start = true,
            "--level-select" => a.level_select = Some(val()?),
            "--car" => a.car = Some(val()?.parse().map_err(|_| "--car N")?),
            "--check" => a.check = true,
            "--drive" => a.drive = Some(val()?.parse().map_err(|_| "--drive SECS")?),
            "--save-mod" => a.save_mod = Some(val()?.into()),
            "-h" | "--help" => {
                println!("{}", include_str!("main.rs").lines().take_while(|l| l.starts_with("//!")).map(|l| l.trim_start_matches("//!").trim_start_matches(' ')).collect::<Vec<_>>().join("\n"));
                std::process::exit(0);
            }
            _ => return Err(format!("unknown option {k} (--help)")),
        }
    }
    Ok(a)
}

fn main() -> eframe::Result {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("hwvx-browser: {e}");
            std::process::exit(2);
        }
    };
    if args.check || args.drive.is_some() || args.save_mod.is_some() {
        std::process::exit(headless(&args));
    }
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([1500.0, 860.0]).with_title("HWVX data browser"),
        ..Default::default()
    };
    eframe::run_native("hwvx-browser", options, Box::new(move |cc| Ok(Box::new(Browser::new(args, cc.egui_ctx.clone())))))
}

/// --check / --drive SECS / --save-mod OUT on --level, without a window (scripts, a package's
/// smoke test): the same lookups and steps as the Level tab's. The exit code: 0 all went well.
fn headless(a: &Args) -> i32 {
    use hwvx_browser::testdrive::{self, Options, TestDrive};
    let Some(level) = &a.level else {
        eprintln!("hwvx-browser: --check, --drive and --save-mod need --level PROJECT_DIR");
        return 2;
    };
    let game = a.game.clone().unwrap_or_else(default_game);
    let game = std::fs::canonicalize(&game).unwrap_or(game);
    let o = Options {
        tools: a.tools.clone().or_else(|| testdrive::default_tools(&game)).unwrap_or_else(|| "tools".into()),
        game,
        data: a.data.clone(),
        build_pack: false,
        project: PathBuf::from(level),
        scratch: a.pref.clone().unwrap_or_else(hwvx_browser::resources::default_pref).join("testdrive"),
        car: a.car,
        skip_intro: a.quick_start,
        start_at: None,
        renderer: a.testdrive_renderer.clone(),
        env: Vec::new(),
    };
    println!("game {} | tools {} | scratch {}", o.game.display(), o.tools.display(), o.scratch.display());
    let mut code = 0;
    if a.check || a.drive.is_some() {
        match testdrive::prepare(&o) {
            Ok(p) => println!("check: ok ({:.0} ms{}) start {}", p.ms, if p.cached { ", cached" } else { "" }, p.start),
            Err(errs) => {
                for e in errs {
                    println!("check: error: {} {} {}", e.file, e.path, e.message);
                }
                return 1;
            }
        }
    }
    if let Some(secs) = a.drive {
        let clicked = Instant::now();
        let p = testdrive::prepare(&o).expect("compiled above");
        match TestDrive::launch(&o, &p, clicked) {
            Err(e) => {
                println!("drive: {e}");
                code = 1;
            }
            Ok(mut d) => {
                println!("drive: the game started (PID {})", d.pid);
                let end = Instant::now() + Duration::from_secs(secs);
                while Instant::now() < end && d.progress.lock().unwrap().driving.is_none() && d.poll().is_none() {
                    std::thread::sleep(Duration::from_millis(50));
                }
                let pr = d.progress.lock().unwrap().clone();
                let how = d.poll();
                d.stop();
                println!("drive: applied {:?}, in the race {:?}, driving {:?}, not applied {:?}, ended {how:?}", pr.applied, pr.in_race, pr.driving, pr.not_applied);
                if pr.driving.is_none() {
                    for l in pr.tail.iter().rev().take(10).rev() {
                        println!("  | {l}");
                    }
                    code = 1;
                }
            }
        }
    }
    if let Some(out) = &a.save_mod {
        match testdrive::share(&o, out) {
            Ok(line) => println!("save-mod: {line}"),
            Err(e) => {
                println!("save-mod: {e}");
                code = 1;
            }
        }
    }
    code
}

/// What a request was for, to route its reply.
enum Pending {
    Packs,
    Load,
    Outline(String),
    Get(String),
    Set { node: String, field: String },
    Camera,
    Shot,
    Frames,
    Pick,
    Textures(String),
    Sounds(String),
    Texture(String),
    Sound(String),
    Play(String),
    Worlds,
    World,
    /// the test drive's "from here": the ground under the camera
    Ground,
    /// the level editor's: a base table's records, a record, the preview applied, a pick, a move
    Records(String),
    Record(String),
    LevelApplied,
    EditPick,
    EditMove,
    SpawnBox(std::path::PathBuf),
    StartCheck(Vec<String>),
    StartSeat,
    ItemQuad(String),
    AddAt(level_edit::AddKind),
    AddItem([f32; 3], level_edit::AddKind),
    Lanes,
    Quiet,
}

/// The largest picture the viewer makes.
const MAX_FRAME: [u32; 2] = [3840, 2160];
/// How long the panel's size has to stay put before the viewer is asked for it.
const RESIZE_SETTLE: Duration = Duration::from_millis(150);

/// What the frame thread shares with the UI: the shared memory it reads, and the newest frame.
#[derive(Default)]
struct Feed {
    reader: Option<FrameReader>,
    frame: Option<Frame>,
    stop: bool,
}

/// The embedded preview: the game's frames as a texture in the central panel.
struct Preview {
    feed: Arc<Mutex<Feed>>,
    texture: Option<egui::TextureHandle>,
    size: [u32; 2],          // the texture's
    frame: u32,              // the viewer's number of the frame shown
    shown: u64,              // frames shown so far
    name: String,            // the shared memory's
    want: [u32; 2],          // the panel's size in physical pixels
    want_since: Instant,
    asked: [u32; 2],         // the size last asked for
    asking: bool,
    fps: f32,
    fps_count: u32,
    fps_t0: Instant,
    events: Vec<Value>,      // input to send with this UI frame
    stats: Value,            // the viewer's "frames" from status
    stats_t: Instant,
}

impl Preview {
    fn new(ctx: &egui::Context) -> Preview {
        let feed = Arc::new(Mutex::new(Feed::default()));
        let f = feed.clone();
        let c = ctx.clone();
        // polls the shared memory: a new frame is copied out here, off the UI thread
        std::thread::spawn(move || {
            loop {
                {
                    let mut g = f.lock().unwrap();
                    if g.stop {
                        break;
                    }
                    let got = g.reader.as_mut().and_then(|r| r.take());
                    if let Some(fr) = got {
                        g.frame = Some(fr);
                        c.request_repaint();
                    }
                }
                std::thread::sleep(Duration::from_millis(2));
            }
        });
        let now = Instant::now();
        Preview {
            feed,
            texture: None,
            size: [0, 0],
            frame: 0,
            shown: 0,
            name: String::new(),
            want: [0, 0],
            want_since: now,
            asked: [0, 0],
            asking: false,
            fps: 0.0,
            fps_count: 0,
            fps_t0: now,
            events: Vec::new(),
            stats: Value::Null,
            stats_t: now,
        }
    }

    /// A new connection: the old shared memory is gone with the old game.
    fn reset(&mut self) {
        let mut g = self.feed.lock().unwrap();
        g.reader = None;
        g.frame = None;
        self.name.clear();
        self.asked = [0, 0];
        self.asking = false;
    }
}

impl Drop for Preview {
    fn drop(&mut self) {
        self.feed.lock().unwrap().stop = true;
    }
}

#[derive(Clone)]
struct PackEntry {
    archive: String,
    type_name: String,
    id: u64,
    pack: String,
    loaded: bool,
}

#[derive(Clone)]
struct Node {
    id: String,
    name: String,
    kind: String,
    type_name: Option<String>,
    offset: Option<u64>,
    size: Option<u64>,
    /// id and type, lowercase: what Find searches
    search: String,
}

#[derive(Default)]
struct Outline {
    pack: String,
    info: Value,
    nodes: Vec<Node>,
    children: HashMap<String, Vec<usize>>, // parent id ("" for roots) -> nodes
    shown: Vec<bool>,                      // with raw nodes hidden: those still drawn
    found: Option<(String, bool, Vec<usize>)>, // Find's last filter, raw shown, and its matches
}

struct Browser {
    args: Args,
    ctx: egui::Context,
    viewer: Option<Viewer>,
    /// The worker thread starting or connecting to the viewer.
    connecting: Option<Receiver<Result<Viewer, String>>>,
    connect_error: Option<String>,
    pending: HashMap<u64, (Pending, Instant)>,
    hello: Value,
    status: String,
    status_error: bool,

    packs: Vec<PackEntry>,
    pack_filter: String,
    type_filter: String,

    outline: Option<Outline>,
    show_raw: bool,
    node_filter: String,
    selected: Option<String>,
    detail: Option<Value>,
    edits: HashMap<String, String>, // field path -> text being edited
    raw_offset: String,
    raw_type: String,
    raw_value: String,

    camera: Value,
    spin: bool,
    shot_path: String,

    script_load_sent: bool,
    script_select_sent: bool,
    app_shot_state: u32,
    app_shot_done: Arc<Mutex<Option<Result<String, String>>>>,
    started: Instant,
    preview: Preview,
    media: MediaState,
    media_reader: MediaReader,
    world: WorldState,
    level: LevelState,
}

impl Browser {
    fn new(args: Args, ctx: egui::Context) -> Browser {
        let shot_dir = std::env::current_dir().unwrap_or_default();
        let mut b = Browser {
            viewer: None,
            connecting: None,
            connect_error: None,
            pending: HashMap::new(),
            hello: Value::Null,
            status: String::new(),
            status_error: false,
            packs: Vec::new(),
            pack_filter: String::new(),
            type_filter: String::new(),
            outline: None,
            show_raw: false,
            node_filter: String::new(),
            selected: None,
            detail: None,
            edits: HashMap::new(),
            raw_offset: "0".into(),
            raw_type: "u32".into(),
            raw_value: String::new(),
            camera: Value::Null,
            spin: false,
            shot_path: shot_dir.join("viewer-shot.png").display().to_string(),
            script_load_sent: false,
            script_select_sent: false,
            app_shot_state: 0,
            app_shot_done: Arc::new(Mutex::new(None)),
            started: Instant::now(),
            preview: Preview::new(&ctx),
            media: MediaState::default(),
            media_reader: MediaReader::new(),
            world: WorldState::default(),
            level: LevelState::default(),
            args,
            ctx,
        };
        if let Some(l) = b.args.level.clone() {
            b.open_level(&l);
            b.level.tab = true;
            b.level.skip_intro = b.args.quick_start;
            b.level.car = b.args.car.unwrap_or(0);
        }
        b.connect();
        b
    }

    /// Starts (or connects to) the viewer on a worker thread: starting the game takes seconds.
    fn connect(&mut self) {
        self.viewer = None; // (a game this started is killed)
        self.drop_all_pending();
        self.level.edit.reset_marks();
        self.connect_error = None;
        self.preview.reset();
        let args = self.args.clone();
        let ctx = self.ctx.clone();
        let (tx, rx) = std::sync::mpsc::channel();
        std::thread::spawn(move || {
            let result = if let Some(port) = args.port {
                let token = args.token.clone().or_else(|| std::env::var("HWVX_VIEWER_TOKEN").ok()).unwrap_or_default();
                Viewer::connect(port, &token)
            } else {
                let game = args.game.clone().unwrap_or_else(default_game);
                let game = std::fs::canonicalize(&game).unwrap_or(game);
                // the editor's own, never the player's saves and settings
                let pref = args.pref.clone().unwrap_or_else(hwvx_browser::resources::default_pref);
                let log_file = Some(pref.join("viewer.log"));
                Viewer::spawn(&SpawnOptions {
                    schemas: args.schemas.clone().or_else(|| default_schemas(&game)),
                    game,
                    data: args.data.clone(),
                    pref,
                    window: args.window.clone().or(Some("800x600".into())),
                    renderer: None,
                    log_file,
                    token: None,
                    viewer_window: args.viewer_window,
                })
            };
            let _ = tx.send(result); // (the browser closed meanwhile: dropping it kills the game)
            ctx.request_repaint();
        });
        self.connecting = Some(rx);
        self.note("starting the viewer…".into());
    }

    /// The worker's result, once it is in.
    fn check_connecting(&mut self) {
        let Some(rx) = &self.connecting else { return };
        let result = match rx.try_recv() {
            Ok(r) => r,
            Err(TryRecvError::Empty) => return,
            Err(TryRecvError::Disconnected) => Err("the viewer could not be started".into()),
        };
        self.connecting = None;
        match result {
            Ok(v) => {
                self.hello = v.hello.clone();
                self.viewer = Some(v);
                self.connect_error = None;
                self.note(String::new());
                self.request("packs", json!({}), Pending::Packs);
                self.request("worlds", json!({}), Pending::Worlds);
                self.request("status", json!({}), Pending::Camera);
            }
            Err(e) => self.connect_error = Some(e),
        }
    }

    /// Sends a request; true if it went (a flag waiting for its reply is set only then).
    fn request(&mut self, cmd: &str, args: Value, why: Pending) -> bool {
        let Some(v) = self.viewer.as_mut() else { return false };
        match v.send(cmd, args) {
            Ok(seq) => {
                self.pending.insert(seq, (why, Instant::now()));
                true
            }
            Err(e) => {
                self.fail(e);
                false
            }
        }
    }

    /// Requests that will never be answered (the viewer gone or restarted, or too late): what
    /// waits for them is told, so nothing stays stuck (the test drive's ground, a move).
    fn drop_pending(&mut self, seqs: Vec<u64>) {
        for s in seqs {
            if let Some((why, _)) = self.pending.remove(&s) {
                match why {
                    Pending::Ground => self.ground_reply(None),
                    Pending::EditMove => self.edit_move_reply(None),
                    _ => {}
                }
            }
        }
    }

    fn drop_all_pending(&mut self) {
        let all: Vec<u64> = self.pending.keys().copied().collect();
        self.drop_pending(all);
    }

    fn fail(&mut self, e: String) {
        self.status = e;
        self.status_error = true;
    }

    fn note(&mut self, s: String) {
        self.status = s;
        self.status_error = false;
    }

    fn load(&mut self, archive: &str, type_name: &str, id: u64) {
        self.note(format!("loading {archive} {type_name} {id}…"));
        self.request("load", json!({"archive": archive, "type": type_name, "id": id}), Pending::Load);
    }

    fn select(&mut self, node: &str, focus: bool) {
        if media_kind(node).is_some() {
            return self.open_media(node);
        }
        if self.media.view != View::Scene {
            self.media.view = View::Scene;
        }
        let Some(o) = &self.outline else { return };
        let pack = o.pack.clone();
        self.selected = Some(node.to_string());
        self.detail = None;
        self.edits.clear();
        self.request(if focus { "focus" } else { "select" }, json!({"pack": pack, "node": node}), Pending::Quiet);
        self.request("get", json!({"pack": pack, "node": node}), Pending::Get(node.to_string()));
    }

    fn set_field(&mut self, field: &str, value: Value) {
        let (Some(o), Some(node)) = (&self.outline, self.selected.clone()) else { return };
        let pack = o.pack.clone();
        self.request(
            "set",
            json!({"pack": pack, "node": node, "field": field, "value": value}),
            Pending::Set { node, field: field.to_string() },
        );
    }

    fn handle_replies(&mut self) {
        let Some(v) = self.viewer.as_mut() else { return };
        let replies = v.poll();
        let closed = v.closed.clone();
        if let Some(e) = closed {
            // gone: nothing pending will be answered
            self.viewer = None;
            self.drop_all_pending();
            self.connect_error = Some(e);
            return;
        }
        let now = Instant::now();
        let late: Vec<u64> = self.pending.iter().filter(|(_, (_, t))| now.duration_since(*t) > REPLY_TIMEOUT).map(|(s, _)| *s).collect();
        if !late.is_empty() {
            self.drop_pending(late);
            self.fail(format!("the viewer did not answer in {} s", REPLY_TIMEOUT.as_secs()));
        }
        for (seq, r) in replies {
            let why = self.pending.remove(&seq).map(|(p, _)| p).unwrap_or(Pending::Quiet);
            let ok = r.get("ok").and_then(Value::as_bool) == Some(true);
            if matches!(why, Pending::Ground) {
                self.ground_reply(ok.then_some(r));
                continue;
            }
            if matches!(why, Pending::EditMove) {
                self.edit_move_reply(ok.then_some(r));
                continue;
            }
            if !ok {
                let e = r.get("error").and_then(Value::as_str).unwrap_or("failed").to_string();
                self.fail(e);
                continue;
            }
            match why {
                Pending::Packs => {
                    self.packs = r["packs"]
                        .as_array()
                        .map(|a| {
                            a.iter()
                                .map(|p| PackEntry {
                                    archive: p["archive"].as_str().unwrap_or("").into(),
                                    type_name: p["type"].as_str().unwrap_or("").into(),
                                    id: p["id"].as_u64().unwrap_or(0),
                                    pack: p["pack"].as_str().unwrap_or("").into(),
                                    loaded: p["loaded"].as_bool().unwrap_or(false),
                                })
                                .collect()
                        })
                        .unwrap_or_default();
                    self.packs.sort_by(|a, b| (&a.archive, &a.type_name, a.id).cmp(&(&b.archive, &b.type_name, b.id)));
                }
                Pending::Load => {
                    let pack = r["pack"].as_str().unwrap_or("").to_string();
                    self.note(format!("loaded {pack}: {} nodes", r["numNodes"]));
                    for p in &mut self.packs {
                        p.loaded = p.pack == pack || (p.loaded && p.type_name != r["type"].as_str().unwrap_or(""));
                    }
                    self.selected = None;
                    self.detail = None;
                    self.media.open = None;
                    self.media.view = View::Scene;
                    self.world.status = Value::Null; // (a pack shown alone: the world went)
                    self.request("outline", json!({"pack": pack}), Pending::Outline(pack.clone()));
                    self.request("status", json!({}), Pending::Camera);
                }
                Pending::Outline(pack) => {
                    self.outline = Some(build_outline(pack.clone(), r));
                    self.list_media(&pack);
                }
                Pending::Get(node) => {
                    if self.selected.as_deref() == Some(node.as_str()) {
                        self.detail = Some(r);
                    }
                }
                Pending::Set { node, field } => {
                    self.note(format!("{node} {field} = {}", r["value"]));
                    self.edits.remove(&field);
                    if let Some(d) = &mut self.detail {
                        if d["id"].as_str() == Some(node.as_str()) {
                            store_value(d, &field, r["value"].clone());
                        }
                    }
                }
                Pending::Camera => {
                    if let Some(c) = r.get("camera") {
                        self.spin = c["spin"].as_bool().unwrap_or(false);
                        self.world.fly = c["mode"].as_str() == Some("fly");
                        self.camera = c.clone();
                    }
                    if let Some(w) = r.get("world") {
                        let w = w.clone();
                        self.world_status(&w);
                    }
                    if let Some(f) = r.get("frames") {
                        self.preview.stats = f.clone();
                    }
                    if let Some(a) = r.get("audio") {
                        self.media.audio = a.clone();
                    }
                }
                Pending::Frames => {
                    self.preview.asking = false;
                    let name = r["name"].as_str().unwrap_or("").to_string();
                    if r["on"].as_bool() == Some(true) && name != self.preview.name {
                        // a new shared memory (the first, or a bigger one)
                        match FrameReader::open(&name, r["key"].as_str().unwrap_or(""), r["size"].as_u64().unwrap_or(0) as usize) {
                            Ok(reader) => {
                                self.preview.feed.lock().unwrap().reader = Some(reader);
                                self.preview.name = name;
                            }
                            Err(e) => self.fail(format!("frames: {e}")),
                        }
                    }
                }
                Pending::Worlds => self.worlds_reply(r),
                Pending::World => {
                    let w = r["world"].clone();
                    self.note(format!("world {}: {} quadrants, loaded in {:.0} ms", w["name"].as_str().unwrap_or(""), w["quads"], w["loadMs"].as_f64().unwrap_or(0.0)));
                    self.media.open = None;
                    self.media.view = View::Scene;
                    self.world_status(&w);
                    self.request("status", json!({}), Pending::Camera);
                }
                Pending::Pick => {
                    if let Some(k) = r["kind"].as_str() {
                        self.note(format!("picked a {k} at {}", r["at"].as_str().unwrap_or("")));
                    }
                    if let Some(n) = r["node"].as_str() {
                        let n = n.to_string();
                        let ctx = self.ctx.clone();
                        self.reveal(&ctx, &n);
                        self.select(&n, false);
                    } else {
                        self.note("nothing there".into());
                    }
                }
                why @ (Pending::Textures(_) | Pending::Sounds(_) | Pending::Texture(_) | Pending::Sound(_) | Pending::Play(_)) => {
                    self.media_reply(why, r)
                }
                Pending::Ground | Pending::EditMove => {}
                Pending::Records(t) => {
                    self.level.edit.base.insert(t, r["records"].as_array().cloned().unwrap_or_default());
                }
                Pending::Record(path) => {
                    if matches!(&self.level.edit.sel, Some(level_edit::Sel::Base(_, p)) if *p == path) {
                        self.level.edit.record = r;
                    }
                }
                Pending::LevelApplied => {}
                Pending::EditPick => self.edit_pick_reply(r),
                Pending::SpawnBox(f) => {
                    self.level.edit.spawn_box = r["box"].clone();
                    self.level.edit.box_file = Some(f);
                    self.level.edit.recheck();
                }
                Pending::StartCheck(paths) => self.start_check_reply(paths, r),
                Pending::StartSeat => self.start_seat_reply(r),
                Pending::ItemQuad(name) => self.item_quad_reply(&name, r),
                Pending::AddAt(kind) => self.add_at_reply(kind, r),
                Pending::AddItem(pos, kind) => self.add_item_reply(pos, kind, r),
                Pending::Lanes => self.lanes_reply(r),
                Pending::Shot => self.note(format!("saved {} ({}x{})", r["path"].as_str().unwrap_or(""), r["width"], r["height"])),
                Pending::Quiet => {}
            }
        }
    }

    /// --load / --select / --app-shot, in that order, each once the one before is answered.
    fn script(&mut self, ctx: &egui::Context) {
        if self.args.app_shot.is_some() {
            // never waits forever: an error, a lost viewer, or the time running out ends it
            let gave_up = if let Some(e) = &self.connect_error {
                Some(e.clone())
            } else if self.status_error {
                Some(self.status.clone())
            } else if self.started.elapsed() > APP_SHOT_TIMEOUT {
                Some(format!("no screenshot in {} s", APP_SHOT_TIMEOUT.as_secs()))
            } else {
                None
            };
            if let Some(e) = gave_up {
                eprintln!("hwvx-browser: --app-shot: {e}");
                // our games first (exit skips destructors): a test drive's, then the viewer's
                if let Some(d) = self.level.drive.as_mut() {
                    d.stop();
                }
                self.level.drive = None;
                self.viewer = None;
                std::process::exit(1);
            }
            ctx.request_repaint_after(Duration::from_millis(100));
        }
        if self.viewer.is_none() {
            return;
        }
        if let Some((a, t, i)) = self.args.load.clone() {
            if !self.script_load_sent && !self.packs.is_empty() {
                self.script_load_sent = true;
                self.load(&a, &t, i);
            }
            if self.outline.is_none() {
                return;
            }
        }
        if let Some(name) = self.args.world.clone() {
            if !self.script_load_sent && !self.world.list.is_empty() {
                self.script_load_sent = true;
                self.world.tab_worlds = true;
                let e = self.world.list.iter().find(|w| w.name.eq_ignore_ascii_case(&name) || w.world.eq_ignore_ascii_case(&name)).cloned();
                match e {
                    Some(e) => {
                        self.world.filter = e.name.clone();
                        self.load_world(&e);
                        if let Some(v) = self.args.view.clone() {
                            let has = |k: &str| v.split(',').any(|s| s.eq_ignore_ascii_case(k));
                            self.world.no_clip = has("noclip");
                            self.world.all_quads = has("allquads");
                            self.world.no_fog = has("nofog");
                            self.send_view3d();
                        }
                        if let Some(ov) = self.args.overlays.clone() {
                            let m: serde_json::Map<String, Value> = ov.split(',').filter(|s| !s.is_empty()).map(|s| (s.to_string(), Value::Bool(true))).collect();
                            self.request("overlays", Value::Object(m), Pending::Quiet);
                            self.request("status", json!({}), Pending::Camera);
                        }
                    }
                    None => self.fail(format!("no world {name}")),
                }
            }
            if self.outline.is_none() || !self.world.status.is_object() {
                return;
            }
        }
        if let Some(n) = self.args.select.clone() {
            let media = media_kind(&n);
            if !self.script_select_sent && self.outline.is_some() && (media.is_none() || self.media.listed) {
                self.script_select_sent = true;
                self.reveal(ctx, &n);
                self.select(&n, true);
            }
            let ready = match media {
                None => self.detail.is_some(),
                Some(View::Texture) => self.media.tex.as_ref().is_some_and(|t| t.id == n),
                Some(View::Sound) => self.media.snd.as_ref().is_some_and(|t| t.id == n),
                _ => self.media.listed,
            };
            if !ready {
                return;
            }
        }
        let Some(path) = self.args.app_shot.clone() else { return };
        if self.args.test_drive && !self.test_drive_settled() {
            return;
        }
        if !self.pending.is_empty() {
            return;
        }
        if !self.args.viewer_window
            && self.media.view == View::Scene
            && (self.preview.shown < 60 || self.preview.size != self.preview.want || self.preview.fps == 0.0 || self.preview.stats["fps"].as_f64().unwrap_or(0.0) == 0.0)
        {
            return; // the picture, at the panel's size
        }
        self.app_shot_state += 1;
        if self.app_shot_state == 10 {
            // a few frames for the layout to settle, then the window's own pixels
            let done = self.app_shot_done.clone();
            let c = ctx.clone();
            ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot(egui::ScreenshotCallback::new(move |img| {
                *done.lock().unwrap() = Some(save_bmp(&path, &img).map(|_| path.display().to_string()));
                c.request_repaint();
            })));
        }
        if let Some(r) = self.app_shot_done.lock().unwrap().take() {
            match r {
                Ok(p) => println!("hwvx-browser: saved {p}"),
                Err(e) => eprintln!("hwvx-browser: {e}"),
            }
            ctx.send_viewport_cmd(egui::ViewportCommand::Close);
        }
        ctx.request_repaint();
    }

    /// Opens the tree down to a node.
    fn reveal(&mut self, ctx: &egui::Context, node: &str) {
        let mut id = String::new();
        for part in node.split('/') {
            if !id.is_empty() {
                id.push('/');
            }
            id.push_str(part);
            let mut st = egui::collapsing_header::CollapsingState::load_with_default_open(ctx, egui::Id::unique(("node", id.as_str())), false);
            st.set_open(true);
            st.store(ctx);
        }
        for g in ["Objects", "Geometry", "Textures", "Sounds", "Song", "Instruments"] {
            let mut st = egui::collapsing_header::CollapsingState::load_with_default_open(ctx, egui::Id::unique(("group", g)), true);
            st.set_open(true);
            st.store(ctx);
        }
    }

    // ---- panels --------------------------------------------------------------------------------

    fn top_bar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            match (&self.viewer, &self.connect_error) {
                (Some(v), _) => {
                    let h = &self.hello;
                    ui.label(format!(
                        "viewer 127.0.0.1:{}  ·  {} data  ·  {}  ·  protocol {}",
                        v.port,
                        h["release"].as_str().unwrap_or("?"),
                        h["renderer"].as_str().unwrap_or("?"),
                        h["protocol"]
                    ))
                    .on_hover_text(h["data"].as_str().unwrap_or(""));
                }
                (None, Some(_)) => {
                    ui.colored_label(egui::Color32::LIGHT_RED, "not connected");
                }
                _ => {}
            }
            if ui.add_enabled(self.connecting.is_none(), egui::Button::new("Reconnect")).clicked() {
                self.connect();
            }
            ui.separator();
            self.test_drive_button(ui);
            if let Some(o) = &self.outline {
                ui.separator();
                ui.strong(&o.pack);
                let (pack_archive, pack_type, pack_id) = (
                    o.info["archive"].as_str().unwrap_or("").to_string(),
                    o.info["type"].as_str().unwrap_or("").to_string(),
                    o.info["id"].as_u64().unwrap_or(0),
                );
                if let Some(world) = self.world.status["world"].as_str().map(str::to_string) {
                    if ui.button("Reload world").on_hover_text("load it again from the files (edits are dropped)").clicked() {
                        self.request("world", json!({"name": world}), Pending::World);
                    }
                } else if ui.button("Reload pack").on_hover_text("load it again from the files (edits are dropped)").clicked() {
                    self.load(&pack_archive, &pack_type, pack_id);
                }
            }
        });
    }

    pub(crate) fn pack_list(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.label("Filter");
            ui.text_edit_singleline(&mut self.pack_filter);
        });
        let mut types: Vec<String> = self.packs.iter().map(|p| p.type_name.clone()).collect::<HashSet<_>>().into_iter().collect();
        types.sort();
        egui::ComboBox::from_label("type")
            .selected_text(if self.type_filter.is_empty() { "all" } else { &self.type_filter })
            .show_ui(ui, |ui| {
                ui.selectable_value(&mut self.type_filter, String::new(), "all");
                for t in types {
                    ui.selectable_value(&mut self.type_filter, t.clone(), t);
                }
            });
        ui.separator();
        let filter = self.pack_filter.to_lowercase();
        let rows: Vec<PackEntry> = self
            .packs
            .iter()
            .filter(|p| self.type_filter.is_empty() || p.type_name == self.type_filter)
            .filter(|p| filter.is_empty() || format!("{} {} {}", p.archive, p.type_name, p.id).to_lowercase().contains(&filter))
            .cloned()
            .collect();
        let mut load = None;
        let current = self.outline.as_ref().map(|o| o.pack.clone());
        egui::ScrollArea::vertical().id_salt("packs").auto_shrink([false, false]).show_rows(ui, 18.0, rows.len(), |ui, range| {
            for p in &rows[range] {
                let text = format!("{}  {} {}", p.archive, p.type_name, p.id);
                let r = ui.selectable_label(current.as_deref() == Some(p.pack.as_str()), egui::RichText::new(text).monospace());
                let r = r.on_hover_text(if p.loaded { "loaded; double-click to reload" } else { "double-click to load" });
                if r.double_clicked() {
                    load = Some(p.clone());
                }
            }
        });
        if let Some(p) = load {
            self.load(&p.archive, &p.type_name, p.id);
        }
    }

    fn outline_tree(&mut self, ui: &mut egui::Ui) {
        ui.heading("Outline");
        let Some(mut outline) = self.outline.take() else {
            ui.label("Double-click a pack to load it.");
            return;
        };
        let o = &mut outline;
        let mut media_click: Option<String> = None;
        ui.horizontal(|ui| {
            ui.label("Find");
            ui.text_edit_singleline(&mut self.node_filter);
        });
        if ui.checkbox(&mut self.show_raw, "raw nodes (no schema)").changed() {
            o.shown = shown_nodes(&o.nodes, &o.children, self.show_raw);
        }
        ui.separator();
        let mut clicked: Option<(String, bool)> = None;
        let selected = self.selected.clone();
        if !self.node_filter.is_empty() {
            let f = self.node_filter.to_lowercase();
            if o.found.as_ref().is_none_or(|(g, raw, _)| *g != f || *raw != self.show_raw) {
                let hits = (0..o.nodes.len()).filter(|&i| o.shown[i] && o.nodes[i].search.contains(&f)).collect();
                o.found = Some((f, self.show_raw, hits));
            }
            let hits = &o.found.as_ref().unwrap().2;
            let row = ui.spacing().interact_size.y;
            egui::ScrollArea::vertical().id_salt("found").auto_shrink([false, false]).show_rows(ui, row, hits.len(), |ui, range| {
                for &i in &hits[range] {
                    let n = &o.nodes[i];
                    let r = ui.selectable_label(selected.as_deref() == Some(n.id.as_str()), node_label(n, true));
                    if r.clicked() {
                        clicked = Some((n.id.clone(), r.double_clicked()));
                    }
                }
            });
        } else {
            egui::ScrollArea::vertical().id_salt("outline").auto_shrink([false, false]).show(ui, |ui| {
                let roots = o.children.get("").cloned().unwrap_or_default();
                for (group, kinds) in [
                    ("Objects", &["typed", "raw"][..]),
                    ("Geometry", &["geometry"][..]),
                    ("Shaders", &["shader"][..]),
                    ("Triggers", &["trigger"][..]),
                ] {
                    let items: Vec<usize> = roots.iter().copied().filter(|&i| o.shown[i] && kinds.contains(&o.nodes[i].kind.as_str())).collect();
                    if items.is_empty() {
                        continue;
                    }
                    egui::CollapsingHeader::new(format!("{group} ({})", items.len()))
                        .id_salt(("group", group))
                        .default_open(group == "Objects")
                        .show(ui, |ui| {
                            for i in items {
                                tree_node(ui, o, i, selected.as_deref(), &mut clicked);
                            }
                        });
                }
                // textures (and the share packs' it uses), sounds, the song and its bank
                self.media_outline(ui, &mut media_click);
            });
        }
        self.outline = Some(outline);
        if let Some((id, focus)) = clicked {
            self.select(&id, focus);
        }
        if let Some(id) = media_click {
            self.open_media(&id);
        }
    }

    fn properties(&mut self, ui: &mut egui::Ui) {
        if self.media_properties(ui) {
            return;
        }
        let Some(d) = self.detail.clone() else {
            ui.heading("Properties");
            ui.label(if self.selected.is_some() { "…" } else { "Select a node in the outline." });
            return;
        };
        let id = d["id"].as_str().unwrap_or("").to_string();
        ui.horizontal(|ui| {
            ui.heading(&id);
            if let Some(t) = d["type"].as_str() {
                ui.label(egui::RichText::new(t).italics());
            }
        });
        ui.horizontal(|ui| {
            ui.label(format!("{}  ·  offset 0x{:X}  ·  {} bytes", d["kind"].as_str().unwrap_or(""), d["offset"].as_u64().unwrap_or(0), d["size"].as_u64().unwrap_or(0)));
            if ui.button("Focus").on_hover_text("frame it in the preview").clicked() {
                self.select(&id, true);
            }
        });
        ui.separator();
        let mut nav: Option<String> = None;
        egui::ScrollArea::vertical().id_salt("props").auto_shrink([false, false]).show(ui, |ui| {
            if let Some(fields) = d["fields"].as_array() {
                egui::Grid::new("fields").num_columns(3).striped(true).spacing([12.0, 4.0]).show(ui, |ui| {
                    for f in fields {
                        let name = f["name"].as_str().unwrap_or("");
                        let mut tip = format!("{}  @0x{:X}", f["ctype"].as_str().unwrap_or(""), f["offset"].as_u64().unwrap_or(0));
                        if let Some(c) = f["confidence"].as_str() {
                            tip += &format!("  ({c})");
                        }
                        if let Some(desc) = f["description"].as_str() {
                            tip = format!("{desc}\n{tip}");
                        }
                        ui.label(name).on_hover_text(tip);
                        ui.vertical(|ui| self.value_editor(ui, name, &f["value"], f.get("link").is_some(), &mut nav));
                        let mut right = f["ctype"].as_str().unwrap_or("").to_string();
                        if let Some(u) = f["units"].as_str() {
                            right = format!("{u}  ·  {right}");
                        }
                        ui.label(egui::RichText::new(right).weak().small());
                        ui.end_row();
                    }
                });
            } else {
                let mut summary = d.clone();
                if let Some(m) = summary.as_object_mut() {
                    for k in ["seq", "ok", "id", "kind", "type", "offset", "size", "bytes", "slots"] {
                        m.remove(k);
                    }
                }
                if summary.as_object().is_some_and(|m| !m.is_empty()) {
                    ui.monospace(serde_json::to_string_pretty(&summary).unwrap_or_default());
                }
                if let Some(slots) = d["slots"].as_array() {
                    ui.separator();
                    ui.label("Links");
                    for s in slots {
                        ui.horizontal(|ui| {
                            ui.monospace(format!("+0x{:X} {}", s["offset"].as_u64().unwrap_or(0), s["kind"].as_str().unwrap_or("")));
                            if let Some(t) = s["target"].as_str() {
                                if ui.link(t).clicked() {
                                    nav = Some(t.to_string());
                                }
                            }
                        });
                    }
                }
                if let Some(b) = d["bytes"].as_str() {
                    ui.separator();
                    ui.label("Bytes");
                    ui.monospace(hex_dump(b));
                }
            }
            self.textures_of(ui, &id);
            if d["kind"].as_str() != Some("texture") {
                ui.separator();
                ui.label("Set raw bytes: @offset (hex, in this node) as a type");
                ui.horizontal(|ui| {
                    ui.label("@");
                    ui.add(egui::TextEdit::singleline(&mut self.raw_offset).desired_width(60.0));
                    egui::ComboBox::from_id_salt("rawtype").selected_text(self.raw_type.clone()).show_ui(ui, |ui| {
                        for t in ["u8", "s8", "u16", "s16", "u32", "s32", "f32"] {
                            ui.selectable_value(&mut self.raw_type, t.to_string(), t);
                        }
                    });
                    ui.add(egui::TextEdit::singleline(&mut self.raw_value).desired_width(100.0));
                    if ui.button("Set").clicked() {
                        match serde_json::from_str::<Value>(self.raw_value.trim()) {
                            Ok(v) => {
                                let field = format!("@{}:{}", self.raw_offset.trim().trim_start_matches("0x"), self.raw_type);
                                self.set_field(&field, v);
                                let node = id.clone();
                                let pack = self.outline.as_ref().map(|o| o.pack.clone()).unwrap_or_default();
                                self.request("get", json!({"pack": pack, "node": node}), Pending::Get(node.clone()));
                            }
                            Err(e) => self.fail(format!("not a number: {e}")),
                        }
                    }
                });
            }
        });
        if let Some(t) = nav {
            self.follow(&t);
        }
    }

    /// One field's value: numbers and strings edited in place (Enter or leaving the box sends
    /// it), arrays element by element, embedded structs member by member, links followable.
    fn value_editor(&mut self, ui: &mut egui::Ui, path: &str, v: &Value, link: bool, nav: &mut Option<String>) {
        match v {
            Value::Array(items) if items.iter().all(Value::is_number) && items.len() <= 4 => {
                ui.horizontal(|ui| {
                    for (i, x) in items.iter().enumerate() {
                        self.scalar_editor(ui, &format!("{path}/{i}"), x, false, nav, 70.0);
                    }
                });
            }
            Value::Array(items) => {
                egui::CollapsingHeader::new(format!("[{}]", items.len())).id_salt(("arr", path)).show(ui, |ui| {
                    for (i, x) in items.iter().enumerate() {
                        ui.horizontal(|ui| {
                            ui.label(format!("{i}"));
                            ui.vertical(|ui| self.value_editor(ui, &format!("{path}/{i}"), x, link || x.is_string(), nav));
                        });
                    }
                });
            }
            Value::Object(m) => {
                egui::CollapsingHeader::new(format!("{{{}}}", m.len())).id_salt(("obj", path)).show(ui, |ui| {
                    for (k, x) in m {
                        ui.horizontal(|ui| {
                            ui.label(k);
                            ui.vertical(|ui| self.value_editor(ui, &format!("{path}/{k}"), x, false, nav));
                        });
                    }
                });
            }
            _ => self.scalar_editor(ui, path, v, link, nav, 160.0),
        }
    }

    fn scalar_editor(&mut self, ui: &mut egui::Ui, path: &str, v: &Value, link: bool, nav: &mut Option<String>, width: f32) {
        let shown = match v {
            Value::String(s) => s.clone(),
            // the data's reals are f32: their shortest spelling (0.7, not 0.699999988)
            Value::Number(n) if !n.is_i64() && !n.is_u64() => format!("{}", n.as_f64().unwrap_or(0.0) as f32),
            other => other.to_string(),
        };
        ui.horizontal(|ui| {
            let mut text = self.edits.get(path).cloned().unwrap_or_else(|| shown.clone());
            let r = ui.add(egui::TextEdit::singleline(&mut text).desired_width(width));
            if r.changed() {
                self.edits.insert(path.to_string(), text.clone());
            }
            let enter = r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
            if (enter || r.lost_focus()) && text != shown && self.edits.contains_key(path) {
                let t = text.trim();
                let value = match (v, serde_json::from_str::<Value>(t)) {
                    (Value::String(_), _) | (Value::Null, _) if link => {
                        if t.is_empty() || t == "null" { Value::Null } else { Value::String(t.to_string()) }
                    }
                    (_, Ok(x)) => x,
                    (_, Err(_)) => Value::String(t.to_string()),
                };
                self.set_field(path, value);
            }
            if link {
                if let Value::String(s) = v {
                    if ui.small_button("→").on_hover_text("go to it").clicked() {
                        *nav = Some(s.clone());
                    }
                }
            }
        });
    }

    /// A link's target: a node of this pack, or of another loaded one (base://ARCHIVE/PACK/node).
    fn follow(&mut self, target: &str) {
        let t = target.trim_start_matches('@');
        if media_kind(t) == Some(View::Texture) && self.media.textures.iter().any(|x| x["id"].as_str() == Some(t)) {
            let ctx = self.ctx.clone();
            self.reveal(&ctx, t);
            return self.open_media(t);
        }
        if let Some(rest) = target.strip_prefix("base://") {
            let pack: Vec<&str> = rest.splitn(3, '/').collect();
            if pack.len() == 3 && self.outline.as_ref().map(|o| o.pack.as_str()) == Some(&format!("{}/{}", pack[0], pack[1])) {
                return self.follow(pack[2]);
            }
            self.note(format!("{target} is in another pack: load it to follow the link"));
            return;
        }
        let exists = self.outline.as_ref().is_some_and(|o| o.nodes.iter().any(|n| n.id == t));
        if exists {
            let ctx = self.ctx.clone();
            self.reveal(&ctx, t);
            self.select(t, false);
        } else {
            self.note(format!("{target}: not a node of the outline"));
        }
    }

    /// The game's picture, at the panel's size in physical pixels (asked for once the size has
    /// settled; meanwhile the last frame is stretched), and the pointer and keys over it.
    fn preview_panel(&mut self, ui: &mut egui::Ui) {
        let ctx = ui.ctx().clone();
        let ppp = ctx.pixels_per_point();
        let status_h = ui.text_style_height(&egui::TextStyle::Body) + 6.0;
        let full = ui.available_rect_before_wrap();
        let rect = egui::Rect::from_min_max(full.min, egui::pos2(full.max.x, (full.max.y - status_h).max(full.min.y + 16.0)));
        let id = egui::Id::unique("hwvx-preview");
        let resp = ui.interact(rect, id, egui::Sense::click_and_drag());
        let mut fly_events = Vec::new();
        if (resp.has_focus() || resp.hovered()) && !ctx.memory(|m| m.focused().is_some_and(|f| f != id)) {
            self.fly_keys(ui, &mut fly_events);
        }
        let world_line = if self.world.status.is_object() {
            let w = &self.world.status;
            format!("  ·  {}: {} of {} quadrants loaded, camera in {}", w["name"].as_str().unwrap_or(""), w["loaded"], w["quads"], w["cameraQuad"])
        } else {
            String::new()
        };
        let editing = self.level_editing();
        let p = &mut self.preview;
        p.events.extend(fly_events);

        // a new frame from the feed: into the texture
        let fresh = p.feed.lock().unwrap().frame.take();
        if let Some(f) = fresh {
            let img = egui::ColorImage::from_rgba_premultiplied([f.width as usize, f.height as usize], &f.rgba);
            match &mut p.texture {
                Some(t) => t.set(img, egui::TextureOptions::LINEAR),
                None => p.texture = Some(ctx.load_texture("preview", img, egui::TextureOptions::LINEAR)),
            }
            p.size = [f.width, f.height];
            p.frame = f.frame;
            p.shown += 1;
            p.fps_count += 1;
        }
        let el = p.fps_t0.elapsed();
        if el >= Duration::from_millis(500) {
            p.fps = p.fps_count as f32 / el.as_secs_f32();
            p.fps_count = 0;
            p.fps_t0 = Instant::now();
        }

        // the size: physical pixels, capped (keeping the shape)
        let phys = rect.size() * ppp;
        let k = (MAX_FRAME[0] as f32 / phys.x).min(MAX_FRAME[1] as f32 / phys.y).min(1.0);
        let want = [((phys.x * k).round() as u32).max(16), ((phys.y * k).round() as u32).max(16)];
        if want != p.want {
            p.want = want;
            p.want_since = Instant::now();
        }
        let ask = self.viewer.is_some() && !p.asking && p.want != p.asked && p.want_since.elapsed() >= RESIZE_SETTLE;
        if p.want != p.asked {
            ctx.request_repaint_after(RESIZE_SETTLE);
        }

        // the picture (stretched while the size settles)
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, egui::Color32::from_rgb(0x30, 0x34, 0x3C));
        if let Some(t) = &p.texture {
            painter.image(t.id(), rect, egui::Rect::from_min_max(egui::pos2(0.0, 0.0), egui::pos2(1.0, 1.0)), egui::Color32::WHITE);
        } else {
            painter.text(rect.center(), egui::Align2::CENTER_CENTER, if self.args.viewer_window { "the preview is in the game's own window" } else { "waiting for the game's picture…" }, egui::FontId::proportional(14.0), ui.visuals().weak_text_color());
        }
        if resp.has_focus() {
            painter.rect_stroke(rect.shrink(1.0), 0.0, egui::Stroke::new(1.0, ui.visuals().selection.stroke.color), egui::StrokeKind::Inside);
        }

        // input, in image pixels
        let scale = if p.size[1] > 0 { p.size[1] as f32 / (rect.height() * ppp).max(1.0) } else { 1.0 } * ppp;
        let shift = ui.input(|i| i.modifiers.shift);
        let d = resp.drag_delta() * scale;
        let cmd = ui.input(|i| i.modifiers.command);
        let mut edit_drag = None;
        if editing && cmd && resp.dragged_by(egui::PointerButton::Primary) {
            // the level editor: the selection follows the ground under the pointer
            if let Some(pos) = resp.interact_pointer_pos() {
                let v = (pos - rect.min) * scale;
                edit_drag = Some((v.x, v.y, resp.drag_started()));
            }
        } else if d != egui::Vec2::ZERO {
            let button = if resp.dragged_by(egui::PointerButton::Secondary) {
                "right"
            } else if resp.dragged_by(egui::PointerButton::Middle) {
                "middle"
            } else {
                "left"
            };
            p.events.push(json!({"type": "drag", "button": button, "pan": shift, "dx": d.x, "dy": d.y}));
        }
        if resp.drag_started() || resp.clicked() {
            resp.request_focus();
        }
        let mut edit_rotate = 0.0f32;
        if resp.hovered() {
            let wheel: f32 = ui.input(|i| {
                i.events
                    .iter()
                    .map(|e| match e {
                        egui::Event::MouseWheel { unit, delta, .. } => match unit {
                            egui::MouseWheelUnit::Point => delta.y / 50.0,
                            egui::MouseWheelUnit::Line => delta.y,
                            egui::MouseWheelUnit::Page => delta.y * 10.0,
                        },
                        _ => 0.0,
                    })
                    .sum()
            });
            if wheel != 0.0 && editing && cmd {
                edit_rotate = wheel;
            } else if wheel != 0.0 {
                p.events.push(json!({"type": "wheel", "dy": wheel}));
            }
        }
        let typing = ctx.memory(|m| m.focused().is_some_and(|f| f != id));
        if (resp.has_focus() || resp.hovered()) && !typing {
            use egui::Key;
            for (key, name) in [
                (Key::F, "f"),
                (Key::Home, "home"),
                (Key::R, "r"),
                (Key::Space, "space"),
                (Key::ArrowLeft, "left"),
                (Key::ArrowRight, "right"),
                (Key::ArrowUp, "up"),
                (Key::ArrowDown, "down"),
                (Key::Plus, "plus"),
                (Key::Equals, "plus"),
                (Key::Minus, "minus"),
                (Key::G, "g"),
            ] {
                if ui.input(|i| i.key_pressed(key)) {
                    p.events.push(json!({"type": "key", "key": name}));
                }
            }
        }
        let click = if resp.clicked() { resp.interact_pointer_pos() } else { None };
        let pick = click.map(|pos| {
            let v = (pos - rect.min) * scale;
            json!({"x": v.x, "y": v.y})
        });

        // the status line
        let (fps, size, shown) = (p.fps, p.size, p.shown);
        let stats = p.stats.clone();
        let events = std::mem::take(&mut p.events);
        let image_h = p.size[1];
        let line = egui::Rect::from_min_max(egui::pos2(full.min.x, rect.max.y), full.max);
        ui.scope_builder(egui::UiBuilder::new().max_rect(line), |ui| {
            ui.horizontal(|ui| {
                let mut s = format!("{fps:.0} fps  ·  {}x{}", size[0], size[1]);
                if size != want && shown > 0 {
                    s += &format!(" (→ {}x{})", want[0], want[1]);
                }
                if let Some(ms) = stats["ms"].as_f64() {
                    s += &format!("  ·  readback {ms:.2} ms/frame, {:.0} published/s", stats["fps"].as_f64().unwrap_or(0.0));
                }
                s += &world_line;
                ui.label(egui::RichText::new(s).weak().small());
            });
        });

        if ask {
            self.preview.asked = want;
            self.preview.asking = true;
            self.request("frames", json!({"width": want[0], "height": want[1]}), Pending::Frames);
        }
        if !events.is_empty() {
            self.request("input", json!({"height": image_h, "events": events}), Pending::Camera);
        }
        if let Some(pk) = pick {
            if editing {
                self.edit_pick(pk["x"].as_f64().unwrap_or(0.0) as f32, pk["y"].as_f64().unwrap_or(0.0) as f32);
            } else {
                self.request("pick", pk, Pending::Pick);
            }
        }
        if let Some((x, y, started)) = edit_drag {
            self.edit_drag(x, y, started);
        }
        if edit_rotate != 0.0 {
            self.edit_rotate(edit_rotate.signum());
        }
    }

    /// status every second (four times a second while a song plays): the readback's stats, the audio.
    fn poll_status(&mut self, ctx: &egui::Context) {
        let every = if self.media.audio["song"] == true { Duration::from_millis(250) } else { Duration::from_secs(1) };
        if self.viewer.is_some() && self.preview.stats_t.elapsed() >= every {
            self.preview.stats_t = Instant::now();
            self.request("status", json!({}), Pending::Camera);
        }
        ctx.request_repaint_after(every);
    }

    fn bottom_bar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.label("Camera");
            let orbit = |b: &mut Browser, args: Value| b.request("orbit", args, Pending::Camera);
            if ui.button("⟲").on_hover_text("orbit left").clicked() {
                orbit(self, json!({"dyaw": -15}));
            }
            if ui.button("⟳").on_hover_text("orbit right").clicked() {
                orbit(self, json!({"dyaw": 15}));
            }
            if ui.button("▲").on_hover_text("orbit up").clicked() {
                orbit(self, json!({"dpitch": 10}));
            }
            if ui.button("▼").on_hover_text("orbit down").clicked() {
                orbit(self, json!({"dpitch": -10}));
            }
            if ui.button("+").on_hover_text("zoom in").clicked() {
                orbit(self, json!({"zoom": 0.8}));
            }
            if ui.button("−").on_hover_text("zoom out").clicked() {
                orbit(self, json!({"zoom": 1.25}));
            }
            if ui.checkbox(&mut self.spin, "spin").changed() {
                let s = self.spin;
                self.request("camera", json!({"spin": s}), Pending::Camera);
            }
            if ui.button("Whole pack").on_hover_text("draw and frame all of it").clicked() {
                if let Some(o) = &self.outline {
                    let pack = o.pack.clone();
                    self.selected = None;
                    self.detail = None;
                    self.request("focus", json!({"pack": pack, "node": null}), Pending::Quiet);
                }
            }
            if let Some(c) = self.camera.as_object() {
                ui.label(
                    egui::RichText::new(format!(
                        "yaw {:.0}°  pitch {:.0}°  distance {:.1}",
                        c.get("yaw").and_then(Value::as_f64).unwrap_or(0.0),
                        c.get("pitch").and_then(Value::as_f64).unwrap_or(0.0),
                        c.get("distance").and_then(Value::as_f64).unwrap_or(0.0)
                    ))
                    .weak(),
                );
            }
            ui.separator();
            ui.add(egui::TextEdit::singleline(&mut self.shot_path).desired_width(220.0));
            if ui.button("Screenshot").on_hover_text("the preview's next frame, as .png or .bmp").clicked() {
                let p = self.shot_path.clone();
                self.request("screenshot", json!({"path": p}), Pending::Shot);
            }
        });
        if !self.status.is_empty() {
            let c = if self.status_error { egui::Color32::LIGHT_RED } else { ui.visuals().weak_text_color() };
            ui.colored_label(c, &self.status);
        }
    }
}

impl eframe::App for Browser {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        self.check_connecting();
        self.handle_replies();
        self.script(&ctx);
        self.level_tick(&ctx);
        self.edit_tick(&ctx);
        if !self.pending.is_empty() || self.connecting.is_some() {
            ctx.request_repaint_after(Duration::from_millis(30));
        }
        if self.connecting.is_some() {
            egui::CentralPanel::default().show(ui, |ui| {
                ui.heading("Starting the viewer…");
                ui.label(if self.args.port.is_some() { "connecting to the running game" } else { "the game is starting" });
                ui.spinner();
            });
            return;
        }
        if let Some(e) = self.connect_error.clone() {
            egui::CentralPanel::default().show(ui, |ui| {
                ui.heading("The viewer could not be started");
                ui.monospace(e);
                if ui.button("Try again").clicked() {
                    self.connect();
                }
            });
            return;
        }
        egui::Panel::top("top").show(ui, |ui| self.top_bar(ui));
        egui::Panel::bottom("bottom").show(ui, |ui| self.bottom_bar(ui));
        egui::Panel::left("packs").default_size(220.0).resizable(true).show(ui, |ui| self.left_tabs(ui));
        egui::Panel::left("outline").default_size(300.0).resizable(true).show(ui, |ui| self.outline_tree(ui));
        egui::Panel::right("properties").default_size(340.0).resizable(true).show(ui, |ui| {
            if self.level.tab && self.level.edit.proj.is_some() {
                self.edit_inspector(ui);
            } else {
                self.properties(ui);
            }
        });
        egui::CentralPanel::default().frame(egui::Frame::NONE).show(ui, |ui| {
            self.view_tabs(ui);
            if self.media.view == View::Scene {
                self.preview_panel(ui);
            } else {
                self.media_panel(ui);
            }
        });
        self.poll_status(&ctx);
    }
}

fn node_label(n: &Node, full: bool) -> egui::RichText {
    let name = if full { &n.id } else { &n.name };
    let text = match &n.type_name {
        Some(t) if !name.starts_with(t.as_str()) => format!("{name}  ·  {t}"),
        _ => name.clone(),
    };
    let r = egui::RichText::new(text);
    if n.kind == "raw" { r.weak() } else { r }
}

fn tree_node(ui: &mut egui::Ui, o: &Outline, i: usize, selected: Option<&str>, clicked: &mut Option<(String, bool)>) {
    let n = &o.nodes[i];
    let kids: Vec<usize> = o.children.get(&n.id).map(|k| k.iter().copied().filter(|&k| o.shown[k]).collect()).unwrap_or_default();
    let is_sel = selected == Some(n.id.as_str());
    let mut label = |ui: &mut egui::Ui| {
        let r = ui.selectable_label(is_sel, node_label(n, false));
        let r = r.on_hover_text(format!("{}\n{}", n.id, n.offset.map(|o| format!("0x{o:X}, {} bytes", n.size.unwrap_or(0))).unwrap_or_default()));
        if r.clicked() || r.double_clicked() {
            *clicked = Some((n.id.clone(), r.double_clicked()));
        }
    };
    if kids.is_empty() {
        ui.horizontal(|ui| {
            ui.add_space(18.0);
            label(ui);
        });
        return;
    }
    egui::collapsing_header::CollapsingState::load_with_default_open(ui.ctx(), egui::Id::unique(("node", n.id.as_str())), false)
        .show_header(ui, |ui| label(ui))
        .body(|ui| {
            for k in kids {
                tree_node(ui, o, k, selected, clicked);
            }
        });
}

fn build_outline(pack: String, mut r: Value) -> Outline {
    let list = r.as_object_mut().and_then(|m| m.remove("nodes")).unwrap_or(Value::Null);
    let mut nodes = Vec::new();
    let mut parents = Vec::new();
    for n in list.as_array().into_iter().flatten() {
        nodes.push(Node {
            id: n["id"].as_str().unwrap_or("").into(),
            name: n["name"].as_str().unwrap_or("").into(),
            kind: n["kind"].as_str().unwrap_or("").into(),
            type_name: n["type"].as_str().map(String::from),
            offset: n["offset"].as_u64(),
            size: n["size"].as_u64(),
            search: format!("{}\u{0}{}", n["id"].as_str().unwrap_or(""), n["type"].as_str().unwrap_or("")).to_lowercase(),
        });
        parents.push(n["parent"].as_str().unwrap_or("").to_string());
    }
    let mut children: HashMap<String, Vec<usize>> = HashMap::new();
    for (i, p) in parents.into_iter().enumerate() {
        children.entry(p).or_default().push(i);
    }
    let shown = shown_nodes(&nodes, &children, false);
    Outline { pack, info: r, nodes, children, shown, found: None }
}

/// With raw nodes hidden, a raw node still shows when something typed is below it.
fn shown_nodes(nodes: &[Node], children: &HashMap<String, Vec<usize>>, raw: bool) -> Vec<bool> {
    fn visit(i: usize, nodes: &[Node], children: &HashMap<String, Vec<usize>>, raw: bool, out: &mut Vec<Option<bool>>) -> bool {
        if let Some(v) = out[i] {
            return v;
        }
        out[i] = Some(false); // (cycles)
        let mut v = raw || nodes[i].kind != "raw";
        for &k in children.get(&nodes[i].id).map(Vec::as_slice).unwrap_or(&[]) {
            v |= visit(k, nodes, children, raw, out);
        }
        out[i] = Some(v);
        v
    }
    let mut out = vec![None; nodes.len()];
    (0..nodes.len()).map(|i| visit(i, nodes, children, raw, &mut out)).collect()
}

/// A set's answer into the node's field list (path: field/index/member).
fn store_value(d: &mut Value, path: &str, value: Value) {
    let mut parts = path.split('/');
    let Some(first) = parts.next() else { return };
    let Some(fields) = d["fields"].as_array_mut() else { return };
    let Some(f) = fields.iter_mut().find(|f| f["name"].as_str() == Some(first)) else { return };
    let mut at = &mut f["value"];
    for p in parts {
        at = match p.parse::<usize>() {
            Ok(i) if at.is_array() => &mut at[i],
            _ => &mut at[p],
        };
    }
    *at = value;
}

fn hex_dump(hex: &str) -> String {
    let bytes: Vec<&str> = (0..hex.len() / 2).map(|i| &hex[i * 2..i * 2 + 2]).collect();
    bytes
        .chunks(16)
        .enumerate()
        .map(|(row, c)| format!("{:04X}  {}", row * 16, c.join(" ")))
        .collect::<Vec<_>>()
        .join("\n")
}

/// The window's pixels as a 32-bit .bmp (no image crate needed).
fn save_bmp(path: &std::path::Path, img: &egui::ColorImage) -> Result<(), String> {
    let [w, h] = img.size;
    let mut out = Vec::with_capacity(54 + w * h * 4);
    let size = (54 + w * h * 4) as u32;
    out.extend_from_slice(b"BM");
    out.extend_from_slice(&size.to_le_bytes());
    out.extend_from_slice(&[0; 4]);
    out.extend_from_slice(&54u32.to_le_bytes());
    out.extend_from_slice(&40u32.to_le_bytes());
    out.extend_from_slice(&(w as i32).to_le_bytes());
    out.extend_from_slice(&(-(h as i32)).to_le_bytes()); // top-down
    out.extend_from_slice(&1u16.to_le_bytes());
    out.extend_from_slice(&32u16.to_le_bytes());
    out.extend_from_slice(&[0; 24]);
    for p in &img.pixels {
        out.extend_from_slice(&[p.b(), p.g(), p.r(), 255]);
    }
    std::fs::write(path, out).map_err(|e| format!("{}: {e}", path.display()))
}
