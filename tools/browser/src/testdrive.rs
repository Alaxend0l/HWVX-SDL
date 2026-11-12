//! Test drives of a level project (docs-pending/mission-editor.md): the project compiled into the
//! editor's scratch folder (tools/xdl_level.py testdrive: only what changed, its sources hashed),
//! there a mod whose track is the level, and the game started straight into it (HWVX_START with
//! the track's id and the car, HWVX_TESTDRIVE=1: its window in front, and leaving the race for
//! the menus quits it). The scratch folder is the editor's own: the game's preferences, mods and
//! .xdl cache there, never the player's. Only the game this started is ever stopped (its PID).

use serde_json::Value;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

/// A compile error, where it is in the project.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct LevelError {
    /// level.jsonc or triggers/NAME.jsonc ("" when it isn't one place)
    pub file: String,
    /// the place in it: volumes/cp1/radius, param/volumes/0 ...
    pub path: String,
    pub message: String,
}

#[derive(Clone, Debug, Default)]
pub struct Options {
    /// tools/ of the checkout (xdl_level.py)
    pub tools: PathBuf,
    pub python: String,
    pub game: PathBuf,
    pub data: Option<PathBuf>,
    /// The level built into a world pack of its own (tools/xdl_level_pack.py, from `data`);
    /// false: the level file the game applies as the world loads, as a shared mod has it.
    pub build_pack: bool,
    pub project: PathBuf,
    /// The editor's own folder: SCRATCH/pref, SCRATCH/mods, SCRATCH/xdl.
    pub scratch: PathBuf,
    pub car: Option<u32>,
    /// The quick start: the intro screen left out, the start's delays cut.
    pub skip_intro: bool,
    /// Player 1's start moved here: x, y, z, rotY (radians; forward is sin, 0, cos).
    pub start_at: Option<[f32; 4]>,
    pub renderer: Option<String>,
    /// Extra environment for the game (tests: HWVX_AUTOINPUT ...).
    pub env: Vec<(String, String)>,
}

/// What the compile step made.
#[derive(Clone, Debug, Default)]
pub struct Prepared {
    pub start: String,
    pub mods: PathBuf,
    pub level: PathBuf,
    /// nothing had changed: the last build was used
    pub cached: bool,
    /// the tool's own time, and the whole step's (the Python start included)
    pub tool_ms: f64,
    pub ms: f64,
}

/// Compiles the project for a test drive: Ok, or the errors by place.
pub fn prepare(o: &Options) -> Result<Prepared, Vec<LevelError>> {
    let t0 = Instant::now();
    let script = o.tools.join("xdl_level.py");
    let mut cmd = Command::new(if o.python.is_empty() { "python3" } else { &o.python });
    cmd.arg(&script).arg("testdrive").arg(&o.project).arg(&o.scratch);
    if let Some(c) = o.car {
        cmd.args(["--car", &c.to_string()]);
    }
    if o.skip_intro {
        cmd.arg("--skip-intro");
    }
    if let (true, Some(d)) = (o.build_pack, &o.data) {
        // the level built into its own world pack, from the player's data (cached)
        cmd.arg("--data").arg(d).arg("--cache").arg(o.scratch.join("base-cache"));
    }
    let one = |m: String| vec![LevelError { message: m, ..Default::default() }];
    std::fs::create_dir_all(&o.scratch).map_err(|e| one(format!("{}: {e}", o.scratch.display())))?;
    let out = cmd.stdin(Stdio::null()).output().map_err(|e| one(format!("{}: {e}", script.display())))?;
    let text = String::from_utf8_lossy(&out.stdout);
    let line = text.lines().rev().find(|l| l.trim_start().starts_with('{'));
    let v: Value = match line.and_then(|l| serde_json::from_str(l).ok()) {
        Some(v) => v,
        None => return Err(one(format!("xdl_level.py testdrive: {}{}", text.trim(), String::from_utf8_lossy(&out.stderr).trim()))),
    };
    if v["ok"].as_bool() != Some(true) {
        let errs: Vec<LevelError> = v["errors"]
            .as_array()
            .map(|a| {
                a.iter()
                    .map(|e| LevelError {
                        file: e["file"].as_str().unwrap_or("").into(),
                        path: e["path"].as_str().unwrap_or("").into(),
                        message: e["message"].as_str().unwrap_or("").into(),
                    })
                    .collect()
            })
            .unwrap_or_default();
        return Err(if errs.is_empty() { one("the compile failed".into()) } else { errs });
    }
    Ok(Prepared {
        start: v["start"].as_str().unwrap_or("").into(),
        mods: v["mods"].as_str().unwrap_or("").into(),
        level: v["level"].as_str().unwrap_or("").into(),
        cached: v["cached"].as_bool().unwrap_or(false),
        tool_ms: v["ms"].as_f64().unwrap_or(0.0),
        ms: t0.elapsed().as_secs_f64() * 1000.0,
    })
}

/// What the running game said, with when (since the click).
#[derive(Default, Debug, Clone)]
pub struct Progress {
    /// the level applied to its world ("level: ... applied")
    pub applied: Option<Duration>,
    pub in_race: Option<Duration>,
    pub driving: Option<Duration>,
    /// "level: not applied: why"
    pub not_applied: Option<String>,
    pub tail: Vec<String>,
}

/// A running test drive: the game this started.
pub struct TestDrive {
    child: Option<Child>,
    pub pid: u32,
    pub clicked: Instant,
    pub progress: Arc<Mutex<Progress>>,
    pub log: PathBuf,
    exited: Option<String>,
}

impl TestDrive {
    /// Starts the game into the level. `clicked`: when the user asked (for the timings).
    pub fn launch(o: &Options, p: &Prepared, clicked: Instant) -> Result<TestDrive, String> {
        let pref = o.scratch.join("pref");
        std::fs::create_dir_all(&pref).map_err(|e| format!("{}: {e}", pref.display()))?;
        let mut cmd = Command::new(&o.game);
        cmd.arg("--windowed");
        if let Some(d) = &o.data {
            cmd.arg("--data").arg(d);
        }
        if let Some(r) = &o.renderer {
            cmd.env("HWVX_RENDERER", r);
        }
        cmd.env("HWVX_PREF_DIR", &pref)
            .env("HWVX_MODS_DIR", &p.mods)
            .env("HWVX_START", &p.start)
            .env("HWVX_TESTDRIVE", "1")
            .env("HWVX_NO_MOVIE", "1")
            .env("HWVX_NO_DIALOG", "1")
            // the converted packs, kept for the next test drive (the editor's own cache)
            .env("HWVX_XDL", "1")
            .env("HWVX_XDL_WRITE", "1")
            .env("HWVX_XDL_DIR", o.scratch.join("xdl"))
            .env_remove("HWVX_VIEWER")
            .env_remove("HWVX_LEVEL")
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        if let Some(s) = o.start_at {
            cmd.env("HWVX_LEVEL_START", format!("{},{},{},{}", s[0], s[1], s[2], s[3]));
        } else {
            cmd.env_remove("HWVX_LEVEL_START");
        }
        for (k, v) in &o.env {
            cmd.env(k, v);
        }
        let mut child = cmd.spawn().map_err(|e| format!("{}: {e}", o.game.display()))?;
        let pid = child.id();
        let progress = Arc::new(Mutex::new(Progress::default()));
        let log = o.scratch.join("testdrive.log");
        let file = Arc::new(Mutex::new(std::fs::File::create(&log).ok()));
        for pipe in [
            child.stdout.take().map(|p| Box::new(p) as Box<dyn std::io::Read + Send>),
            child.stderr.take().map(|p| Box::new(p) as Box<dyn std::io::Read + Send>),
        ]
        .into_iter()
        .flatten()
        {
            let progress = progress.clone();
            let file = file.clone();
            std::thread::spawn(move || {
                for line in BufReader::new(pipe).lines().map_while(Result::ok) {
                    if let Some(f) = file.lock().unwrap().as_mut() {
                        use std::io::Write;
                        let _ = writeln!(f, "{line}");
                    }
                    let mut p = progress.lock().unwrap();
                    let now = clicked.elapsed();
                    if line.contains("level: ") && line.contains(" applied to ") {
                        p.applied.get_or_insert(now);
                    } else if let Some(i) = line.find("level: not applied: ") {
                        p.not_applied = Some(line[i + 20..].to_string());
                    } else if line.contains("testdrive: in the race") {
                        p.in_race.get_or_insert(now);
                    } else if line.contains("testdrive: driving") {
                        p.driving.get_or_insert(now);
                    }
                    p.tail.push(line);
                    if p.tail.len() > 40 {
                        p.tail.remove(0);
                    }
                }
            });
        }
        Ok(TestDrive { child: Some(child), pid, clicked, progress, log, exited: None })
    }

    /// None while it runs; how it ended once it has (the race left, the window closed).
    pub fn poll(&mut self) -> Option<String> {
        if self.exited.is_none() {
            if let Some(c) = self.child.as_mut() {
                match c.try_wait() {
                    Ok(Some(st)) => {
                        self.exited = Some(if st.success() { "ended".into() } else { format!("ended ({st})") });
                        self.child = None;
                    }
                    Ok(None) => {}
                    Err(e) => self.exited = Some(e.to_string()),
                }
            }
        }
        self.exited.clone()
    }

    /// Stops the game this started (its PID only).
    pub fn stop(&mut self) {
        if let Some(mut c) = self.child.take() {
            let _ = c.kill();
            let _ = c.wait();
            self.exited.get_or_insert_with(|| "stopped".into());
        }
    }

    pub fn running(&mut self) -> bool {
        self.poll().is_none()
    }
}

impl Drop for TestDrive {
    fn drop(&mut self) {
        self.stop();
    }
}

/// tools/ of the checkout the game is in.
/// Save as mod: the project packed into an installable .hwmod at `out` (tools/xdl_level.py mod
/// --hwmod: the compiled level file and the project's sources; tools/xdl_mod.py's no-game-data
/// check against an index of the player's own data, made once into the scratch's cache). The
/// mod folder it packs is made in the scratch. Ok: the packer's line; Err: why it wasn't packed.
pub fn share(o: &Options, out: &Path) -> Result<String, String> {
    let script = o.tools.join("xdl_level.py");
    let stem = out.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_else(|| "level".into());
    let folder = o.scratch.join("share").join(&stem);
    let mut cmd = Command::new(if o.python.is_empty() { "python3" } else { &o.python });
    cmd.arg(&script).arg("mod").arg(&o.project).arg(&folder).arg("--hwmod").arg(out);
    match &o.data {
        Some(d) => cmd.arg("--data").arg(d).arg("--cache").arg(o.scratch.join("base-cache")),
        None => return Err("the game's data folder isn't known (the check for game data needs it)".into()),
    };
    std::fs::create_dir_all(o.scratch.join("share")).map_err(|e| format!("{}: {e}", o.scratch.display()))?;
    let r = cmd.stdin(Stdio::null()).output().map_err(|e| format!("{}: {e}", script.display()))?;
    let text = format!("{}{}", String::from_utf8_lossy(&r.stdout), String::from_utf8_lossy(&r.stderr));
    let last = text.lines().rev().find(|l| !l.trim().is_empty()).unwrap_or("").to_string();
    if r.status.success() {
        Ok(last)
    } else {
        Err(text.trim().to_string())
    }
}

pub fn default_tools(game: &Path) -> Option<PathBuf> {
    game.ancestors().skip(1).map(|d| d.join("tools")).find(|d| d.join("xdl_level.py").is_file())
}

/// The editor's start "from here": below `at` (the ground the viewer's raycast found, a little
/// above it) facing along the camera's yaw (degrees, the viewer's orbit angle: it looks along
/// (-sin, -cos)).
pub fn start_from(ground: [f32; 3], yaw_deg: f32) -> [f32; 4] {
    let yaw = yaw_deg.to_radians();
    let mut rot = yaw + std::f32::consts::PI;
    if rot > std::f32::consts::PI {
        rot -= 2.0 * std::f32::consts::PI;
    }
    [ground[0], ground[1] + 2.0, ground[2], rot]
}

/// The level's summary from level.jsonc (comments and trailing commas allowed).
pub fn read_level(project: &Path) -> Result<Value, String> {
    let p = project.join("level.jsonc");
    let text = std::fs::read_to_string(&p).map_err(|e| format!("{}: {e}", p.display()))?;
    serde_json::from_str(&strip_jsonc(&text)).map_err(|e| format!("{}: {e}", p.display()))
}

/// JSONC to JSON: comments out, then trailing commas out (strings kept as they are).
pub fn strip_jsonc(s: &str) -> String {
    fn pass(s: &str, commas: bool) -> String {
        let b: Vec<char> = s.chars().collect();
        let mut out = String::with_capacity(s.len());
        let mut i = 0;
        while i < b.len() {
            let c = b[i];
            if c == '"' {
                out.push(c);
                i += 1;
                while i < b.len() {
                    out.push(b[i]);
                    if b[i] == '\\' && i + 1 < b.len() {
                        out.push(b[i + 1]);
                        i += 2;
                        continue;
                    }
                    i += 1;
                    if b[i - 1] == '"' {
                        break;
                    }
                }
            } else if !commas && c == '/' && i + 1 < b.len() && b[i + 1] == '/' {
                while i < b.len() && b[i] != '\n' {
                    i += 1;
                }
            } else if !commas && c == '/' && i + 1 < b.len() && b[i + 1] == '*' {
                i += 2;
                while i + 1 < b.len() && !(b[i] == '*' && b[i + 1] == '/') {
                    i += 1;
                }
                i += 2;
            } else if commas && c == ',' {
                let mut j = i + 1;
                while j < b.len() && b[j].is_whitespace() {
                    j += 1;
                }
                if !(j < b.len() && (b[j] == '}' || b[j] == ']')) {
                    out.push(c);
                }
                i += 1;
            } else {
                out.push(c);
                i += 1;
            }
        }
        out
    }
    pass(&pass(s, false), true)
}
