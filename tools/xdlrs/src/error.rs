//! One error type for the crate. `kind` says which Python exception the same input raises in
//! tools/xdl_struct.py / xdl_tac.py, so a caller can catch exactly what the Python catches
//! (e.g. xdl_graph catches TacError around a trigger but lets XdlError through).

use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    /// XdlError
    Xdl,
    /// TacError
    Tac,
    /// ValueError from json.loads (bad JSON after the JSONC is stripped)
    Json,
    /// struct.error (reading past the end of a blob, a table of odd length)
    Struct,
    /// an exception the Python doesn't catch (TypeError, KeyError, OverflowError, ...): the
    /// Python stops with a traceback there; here it is an error like the others
    Uncaught,
    /// a file that can't be read
    Io,
    /// GraphError (raised by a host, e.g. xdl_graph's link_to)
    Graph,
    /// xdl_level's LevelError (a problem of the level project, by its place)
    Level,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    pub kind: Kind,
    pub msg: String,
}

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    pub fn new(kind: Kind, msg: impl Into<String>) -> Self {
        Error { kind, msg: msg.into() }
    }
    pub fn xdl(msg: impl Into<String>) -> Self {
        Self::new(Kind::Xdl, msg)
    }
    pub fn tac(msg: impl Into<String>) -> Self {
        Self::new(Kind::Tac, msg)
    }
    pub fn json(msg: impl Into<String>) -> Self {
        Self::new(Kind::Json, msg)
    }
    pub fn strct(msg: impl Into<String>) -> Self {
        Self::new(Kind::Struct, msg)
    }
    pub fn uncaught(msg: impl Into<String>) -> Self {
        Self::new(Kind::Uncaught, msg)
    }
    pub fn io(msg: impl Into<String>) -> Self {
        Self::new(Kind::Io, msg)
    }
    pub fn graph(msg: impl Into<String>) -> Self {
        Self::new(Kind::Graph, msg)
    }
    pub fn level(msg: impl Into<String>) -> Self {
        Self::new(Kind::Level, msg)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.msg)
    }
}

impl std::error::Error for Error {}

pub(crate) fn read(p: &std::path::Path) -> Result<Vec<u8>> {
    std::fs::read(p).map_err(|e| Error::io(format!("{}: {e}", p.display())))
}

pub(crate) fn read_text(p: &std::path::Path) -> Result<String> {
    std::fs::read_to_string(p).map_err(|e| Error::io(format!("{}: {e}", p.display())))
}
