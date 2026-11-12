//! The connection to HWVX-SDL's viewer mode (src/port/viewer.c; the protocol is
//! docs-pending/viewer-protocol.md): line-delimited JSON over 127.0.0.1 TCP.
//!
//! `Viewer::spawn` runs the game with HWVX_VIEWER=0 and a token of its own (HWVX_VIEWER_TOKEN) and
//! reads the port it listens on from its log; `Viewer::connect` uses one that is already running
//! (its token: the one it was given, or the one it logged). Either opens with `hello` and the token,
//! as the viewer wants. Requests carry a `seq` the reply echoes; replies arrive on a reader thread
//! and are picked up with `poll` (non-blocking, for the UI) or `call` (blocking, for scripts and
//! tests). Both block: the UI runs them on a worker thread.

use serde_json::{Map, Value, json};
use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};
use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::mpsc::{Receiver, RecvTimeoutError, TryRecvError, channel};
use std::time::{Duration, Instant};

/// How to run the game in viewer mode.
#[derive(Clone, Debug, Default)]
pub struct SpawnOptions {
    pub game: PathBuf,
    pub data: Option<PathBuf>,
    pub schemas: Option<PathBuf>,
    /// HWVX_PREF_DIR: never the player's own saves and settings.
    pub pref: PathBuf,
    pub window: Option<String>,
    pub renderer: Option<String>,
    /// Where the game's log is copied (it is also kept in `Viewer::log`).
    pub log_file: Option<PathBuf>,
    /// HWVX_VIEWER_TOKEN for the game; None: one is made up.
    pub token: Option<String>,
}

/// 128 random bits as hex, from the standard library's randomly keyed hasher (no crate needed).
pub fn new_token() -> String {
    use std::hash::{BuildHasher, Hasher};
    let mut out = String::new();
    for i in 0..2u64 {
        let mut h = std::collections::hash_map::RandomState::new().build_hasher();
        h.write_u64(i);
        h.write_u128(std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0));
        h.write_u32(std::process::id());
        out += &format!("{:016x}", h.finish());
    }
    out
}

pub struct Viewer {
    stream: TcpStream,
    replies: Receiver<Value>,
    child: Option<Child>,
    next_seq: u64,
    /// Replies `call` read past while waiting for its own (and events).
    stash: HashMap<u64, Value>,
    pub events: Vec<Value>,
    pub port: u16,
    /// The reply to the opening hello.
    pub hello: Value,
    /// Set once the connection is gone (the game quit or dropped it).
    pub closed: Option<String>,
}

impl Viewer {
    /// A viewer already running, with its token.
    pub fn connect(port: u16, token: &str) -> Result<Viewer, String> {
        let stream = TcpStream::connect(("127.0.0.1", port)).map_err(|e| format!("127.0.0.1:{port}: {e}"))?;
        Self::over(stream, None, port, token)
    }

    pub fn spawn(o: &SpawnOptions) -> Result<Viewer, String> {
        std::fs::create_dir_all(&o.pref).map_err(|e| format!("{}: {e}", o.pref.display()))?;
        let mut cmd = Command::new(&o.game);
        cmd.arg("--windowed");
        if let Some(w) = &o.window {
            cmd.args(["--window", w]);
        }
        if let Some(r) = &o.renderer {
            cmd.args(["--renderer", r]);
        }
        if let Some(d) = &o.data {
            cmd.arg("--data").arg(d);
        }
        let token = o.token.clone().unwrap_or_else(new_token);
        cmd.env("HWVX_VIEWER", "0") // any free port, read back from the log
            .env("HWVX_VIEWER_TOKEN", &token)
            .env("HWVX_PREF_DIR", &o.pref)
            .env("HWVX_NO_DIALOG", "1")
            .env("HWVX_NO_MOVIE", "1")
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        if let Some(s) = &o.schemas {
            cmd.env("HWVX_VIEWER_SCHEMAS", s);
        }
        let mut child = cmd.spawn().map_err(|e| format!("{}: {e}", o.game.display()))?;
        let (tx, rx) = channel::<String>();
        let log = o.log_file.as_ref().and_then(|p| std::fs::File::create(p).ok());
        let log = std::sync::Arc::new(std::sync::Mutex::new(log));
        for pipe in [
            child.stdout.take().map(|p| Box::new(p) as Box<dyn std::io::Read + Send>),
            child.stderr.take().map(|p| Box::new(p) as Box<dyn std::io::Read + Send>),
        ]
        .into_iter()
        .flatten()
        {
            let tx = tx.clone();
            let log = log.clone();
            std::thread::spawn(move || {
                for line in BufReader::new(pipe).lines().map_while(Result::ok) {
                    if let Some(f) = log.lock().unwrap().as_mut() {
                        let _ = writeln!(f, "{line}");
                    }
                    let _ = tx.send(line);
                }
            });
        }
        let end = Instant::now() + Duration::from_secs(60);
        let mut tail = Vec::new();
        let port = loop {
            if let Ok(Some(status)) = child.try_wait() {
                return Err(format!("the game exited ({status}) before listening:\n{}", tail.join("\n")));
            }
            match rx.recv_timeout(Duration::from_millis(100)) {
                Ok(line) => {
                    if let Some(i) = line.find("viewer: listening on 127.0.0.1:") {
                        if let Ok(p) = line[i + 31..].trim().parse::<u16>() {
                            break p;
                        }
                    }
                    tail.push(line);
                    if tail.len() > 30 {
                        tail.remove(0);
                    }
                }
                Err(RecvTimeoutError::Timeout) => {}
                Err(RecvTimeoutError::Disconnected) => {}
            }
            if Instant::now() > end {
                let _ = child.kill();
                let _ = child.wait();
                return Err(format!("the game did not start listening:\n{}", tail.join("\n")));
            }
        };
        let stream = match TcpStream::connect(("127.0.0.1", port)) {
            Ok(s) => s,
            Err(e) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(format!("127.0.0.1:{port}: {e}"));
            }
        };
        Self::over(stream, Some(child), port, &token)
    }

    fn over(stream: TcpStream, child: Option<Child>, port: u16, token: &str) -> Result<Viewer, String> {
        stream.set_nodelay(true).ok();
        let read = stream.try_clone().map_err(|e| e.to_string())?;
        let (tx, rx) = channel();
        std::thread::spawn(move || {
            for line in BufReader::new(read).lines() {
                let Ok(line) = line else { break };
                if line.trim().is_empty() {
                    continue;
                }
                let v = serde_json::from_str::<Value>(&line)
                    .unwrap_or_else(|e| json!({"ok": false, "error": format!("unreadable reply: {e}")}));
                if tx.send(v).is_err() {
                    break;
                }
            }
        });
        let mut v = Viewer {
            stream,
            replies: rx,
            child,
            next_seq: 1,
            stash: HashMap::new(),
            events: Vec::new(),
            port,
            hello: Value::Null,
            closed: None,
        };
        // (on an error, dropping v kills a game it started)
        v.hello = v.call("hello", json!({"token": token}))?;
        Ok(v)
    }

    /// Sends a request; its reply comes back with the returned seq.
    pub fn send(&mut self, cmd: &str, args: Value) -> Result<u64, String> {
        let seq = self.next_seq;
        self.next_seq += 1;
        let mut req = Map::new();
        req.insert("seq".into(), json!(seq));
        req.insert("cmd".into(), json!(cmd));
        if let Value::Object(a) = args {
            req.extend(a);
        }
        let mut line = serde_json::to_string(&Value::Object(req)).unwrap();
        line.push('\n');
        if let Err(e) = self.stream.write_all(line.as_bytes()) {
            let e = format!("the viewer is gone: {e}");
            self.closed = Some(e.clone());
            return Err(e);
        }
        Ok(seq)
    }

    /// The replies that have arrived, (seq, reply); events go to `events`.
    pub fn poll(&mut self) -> Vec<(u64, Value)> {
        let mut out: Vec<(u64, Value)> = self.stash.drain().collect();
        loop {
            match self.replies.try_recv() {
                Ok(v) => match v.get("seq").and_then(Value::as_u64) {
                    Some(seq) => out.push((seq, v)),
                    None if v.get("event").is_some() => self.events.push(v),
                    None => out.push((0, v)),
                },
                Err(TryRecvError::Empty) => break,
                Err(TryRecvError::Disconnected) => {
                    if self.closed.is_none() {
                        self.closed = Some("the viewer closed the connection".into());
                    }
                    break;
                }
            }
        }
        out
    }

    /// One request and its reply; Err with the viewer's message when it answers ok: false.
    pub fn call(&mut self, cmd: &str, args: Value) -> Result<Value, String> {
        let seq = self.send(cmd, args)?;
        let end = Instant::now() + Duration::from_secs(60);
        loop {
            if let Some(v) = self.stash.remove(&seq) {
                return check(cmd, v);
            }
            let left = end.saturating_duration_since(Instant::now());
            match self.replies.recv_timeout(left) {
                Ok(v) => match v.get("seq").and_then(Value::as_u64) {
                    Some(s) if s == seq => return check(cmd, v),
                    Some(s) => {
                        self.stash.insert(s, v);
                    }
                    None if v.get("event").is_some() => self.events.push(v),
                    None => return check(cmd, v),
                },
                Err(RecvTimeoutError::Timeout) => return Err(format!("{cmd}: no reply in 60 s")),
                Err(RecvTimeoutError::Disconnected) => {
                    self.closed = Some("the viewer closed the connection".into());
                    return Err(format!("{cmd}: the viewer closed the connection"));
                }
            }
        }
    }

    pub fn owns_game(&self) -> bool {
        self.child.is_some()
    }

    /// Ends the game this started (force-killed: it keeps no state worth saving).
    pub fn kill(&mut self) {
        if let Some(mut c) = self.child.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }
}

impl Drop for Viewer {
    fn drop(&mut self) {
        let _ = self.stream.shutdown(std::net::Shutdown::Both);
        self.kill();
    }
}

fn check(cmd: &str, v: Value) -> Result<Value, String> {
    if v.get("ok").and_then(Value::as_bool) == Some(true) {
        Ok(v)
    } else {
        Err(format!("{cmd}: {}", v.get("error").and_then(Value::as_str).unwrap_or("failed")))
    }
}

/// The game next to this program's checkout: <repo>/build/hwvx, from tools/browser/target/*/.
pub fn default_game() -> PathBuf {
    let name = if cfg!(windows) { "hwvx.exe" } else { "hwvx" };
    if let Ok(exe) = std::env::current_exe() {
        for dir in exe.ancestors().skip(1) {
            let p = dir.join("build").join(name);
            if p.is_file() {
                return p;
            }
        }
    }
    Path::new("build").join(name)
}

/// schemas/ of the checkout the game is in, if there is one.
pub fn default_schemas(game: &Path) -> Option<PathBuf> {
    game.ancestors().skip(1).map(|d| d.join("schemas")).find(|d| d.join("CarAInfo.schema.json").is_file())
}
