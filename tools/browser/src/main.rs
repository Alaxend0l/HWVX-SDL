//! hwvx-browser: browse (and edit, live) the game's data packs, with the game itself as the
//! preview. It runs HWVX-SDL in viewer mode (HWVX_VIEWER, src/port/viewer.c) and talks to it over
//! the viewer protocol (docs-pending/viewer-protocol.md); the preview is the game's own window.
//!
//!   hwvx-browser [--game build/hwvx] [--data DIR] [--schemas DIR] [--pref DIR]
//!                [--port N]                      a viewer already running
//!                [--load ARCHIVE:TYPE:ID] [--select NODE] [--app-shot FILE.bmp]
//!
//! --app-shot saves this window (once the --load and --select replies are in) and quits:
//! for scripted screenshots of the browser itself.

use eframe::egui;
use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use serde_json::{Value, json};
use std::collections::{HashMap, HashSet};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

#[derive(Default, Clone)]
struct Args {
    game: Option<PathBuf>,
    data: Option<PathBuf>,
    schemas: Option<PathBuf>,
    pref: Option<PathBuf>,
    port: Option<u16>,
    window: Option<String>,
    load: Option<(String, String, u64)>,
    select: Option<String>,
    app_shot: Option<PathBuf>,
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
            "--app-shot" => a.app_shot = Some(val()?.into()),
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
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([1180.0, 760.0]).with_title("HWVX data browser"),
        ..Default::default()
    };
    eframe::run_native("hwvx-browser", options, Box::new(move |cc| Ok(Box::new(Browser::new(args, cc.egui_ctx.clone())))))
}

/// What a request was for, to route its reply.
enum Pending {
    Hello,
    Packs,
    Load,
    Outline(String),
    Get(String),
    Set { node: String, field: String },
    Camera,
    Shot,
    Quiet,
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
}

#[derive(Default)]
struct Outline {
    pack: String,
    info: Value,
    nodes: Vec<Node>,
    children: HashMap<String, Vec<usize>>, // parent id ("" for roots) -> nodes
    shown: Vec<bool>,                      // with raw nodes hidden: those still drawn
}

struct Browser {
    args: Args,
    ctx: egui::Context,
    viewer: Option<Viewer>,
    connect_error: Option<String>,
    pending: HashMap<u64, Pending>,
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
}

impl Browser {
    fn new(args: Args, ctx: egui::Context) -> Browser {
        let shot_dir = std::env::current_dir().unwrap_or_default();
        let mut b = Browser {
            viewer: None,
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
            args,
            ctx,
        };
        b.connect();
        b
    }

    fn connect(&mut self) {
        self.viewer = None;
        let result = if let Some(port) = self.args.port {
            Viewer::connect(port)
        } else {
            let game = self.args.game.clone().unwrap_or_else(default_game);
            let game = std::fs::canonicalize(&game).unwrap_or(game);
            let pref = self.args.pref.clone().unwrap_or_else(|| {
                // next to the program, never the player's own saves and settings
                std::env::current_exe()
                    .ok()
                    .and_then(|e| e.parent().map(|d| d.join("viewer-pref")))
                    .unwrap_or_else(|| PathBuf::from("viewer-pref"))
            });
            let log_file = Some(pref.join("viewer.log"));
            Viewer::spawn(&SpawnOptions {
                schemas: self.args.schemas.clone().or_else(|| default_schemas(&game)),
                game,
                data: self.args.data.clone(),
                pref,
                window: self.args.window.clone().or(Some("800x600".into())),
                renderer: None,
                log_file,
            })
        };
        match result {
            Ok(v) => {
                self.viewer = Some(v);
                self.connect_error = None;
                self.request("hello", json!({}), Pending::Hello);
                self.request("packs", json!({}), Pending::Packs);
                self.request("status", json!({}), Pending::Camera);
            }
            Err(e) => self.connect_error = Some(e),
        }
    }

    fn request(&mut self, cmd: &str, args: Value, why: Pending) {
        let Some(v) = self.viewer.as_mut() else { return };
        match v.send(cmd, args) {
            Ok(seq) => {
                self.pending.insert(seq, why);
            }
            Err(e) => self.fail(e),
        }
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
        for (seq, r) in replies {
            let why = self.pending.remove(&seq).unwrap_or(Pending::Quiet);
            let ok = r.get("ok").and_then(Value::as_bool) == Some(true);
            if !ok {
                let e = r.get("error").and_then(Value::as_str).unwrap_or("failed").to_string();
                self.fail(e);
                continue;
            }
            match why {
                Pending::Hello => self.hello = r,
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
                    self.request("outline", json!({"pack": pack}), Pending::Outline(pack.clone()));
                    self.request("status", json!({}), Pending::Camera);
                }
                Pending::Outline(pack) => self.outline = Some(build_outline(pack, r)),
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
                        self.camera = c.clone();
                    }
                }
                Pending::Shot => self.note(format!("saved {} ({}x{})", r["path"].as_str().unwrap_or(""), r["width"], r["height"])),
                Pending::Quiet => {}
            }
        }
    }

    /// --load / --select / --app-shot, in that order, each once the one before is answered.
    fn script(&mut self, ctx: &egui::Context) {
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
        if let Some(n) = self.args.select.clone() {
            if !self.script_select_sent && self.outline.is_some() {
                self.script_select_sent = true;
                self.reveal(ctx, &n);
                self.select(&n, true);
            }
            if self.detail.is_none() {
                return;
            }
        }
        let Some(path) = self.args.app_shot.clone() else { return };
        if !self.pending.is_empty() {
            return;
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
        for g in ["Objects", "Geometry"] {
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
            if ui.button("Reconnect").clicked() {
                self.pending.clear();
                self.connect();
            }
            if let Some(o) = &self.outline {
                ui.separator();
                ui.strong(&o.pack);
                let (pack_archive, pack_type, pack_id) = (
                    o.info["archive"].as_str().unwrap_or("").to_string(),
                    o.info["type"].as_str().unwrap_or("").to_string(),
                    o.info["id"].as_u64().unwrap_or(0),
                );
                if ui.button("Reload pack").on_hover_text("load it again from the files (edits are dropped)").clicked() {
                    self.load(&pack_archive, &pack_type, pack_id);
                }
            }
        });
    }

    fn pack_list(&mut self, ui: &mut egui::Ui) {
        ui.heading("Packs");
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
        let Some(o) = &mut self.outline else {
            ui.label("Double-click a pack to load it.");
            return;
        };
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
        egui::ScrollArea::vertical().id_salt("outline").auto_shrink([false, false]).show(ui, |ui| {
            if !self.node_filter.is_empty() {
                let f = self.node_filter.to_lowercase();
                for (i, n) in o.nodes.iter().enumerate() {
                    if o.shown[i] && (n.id.to_lowercase().contains(&f) || n.type_name.as_deref().unwrap_or("").to_lowercase().contains(&f)) {
                        let r = ui.selectable_label(selected.as_deref() == Some(n.id.as_str()), node_label(n, true));
                        if r.clicked() {
                            clicked = Some((n.id.clone(), r.double_clicked()));
                        }
                    }
                }
                return;
            }
            let roots = o.children.get("").cloned().unwrap_or_default();
            for (group, kinds) in [
                ("Objects", &["typed", "raw"][..]),
                ("Geometry", &["geometry"][..]),
                ("Textures", &["texture"][..]),
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
        });
        if let Some((id, focus)) = clicked {
            self.select(&id, focus);
        }
    }

    fn properties(&mut self, ui: &mut egui::Ui) {
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
            Value::Array(items) if items.iter().all(|x| !x.is_array() && !x.is_object()) && items.len() <= 4 && !link => {
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
                            ui.vertical(|ui| self.value_editor(ui, &format!("{path}/{i}"), x, link, nav));
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
        self.handle_replies();
        self.script(&ctx);
        if !self.pending.is_empty() {
            ctx.request_repaint_after(std::time::Duration::from_millis(30));
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
        egui::Panel::left("packs").default_size(260.0).resizable(true).show(ui, |ui| self.pack_list(ui));
        egui::Panel::left("outline").default_size(300.0).resizable(true).show(ui, |ui| self.outline_tree(ui));
        egui::CentralPanel::default().show(ui, |ui| self.properties(ui));
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
        });
        parents.push(n["parent"].as_str().unwrap_or("").to_string());
    }
    let mut children: HashMap<String, Vec<usize>> = HashMap::new();
    for (i, p) in parents.into_iter().enumerate() {
        children.entry(p).or_default().push(i);
    }
    let shown = shown_nodes(&nodes, &children, false);
    Outline { pack, info: r, nodes, children, shown }
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
