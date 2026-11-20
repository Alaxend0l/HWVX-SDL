//! The HWVX data browser: an egui front end for HWVX-SDL's viewer mode.
pub mod client;
pub mod frames;
pub mod media;
pub mod shm;
pub mod resources;
pub mod data_dir;
pub mod cars;
pub mod content;
pub mod content_retail;
pub mod content_sounds;
pub mod testdrive;
/// moved to the xdlrs crate (tools/xdlrs), kept here under its old path
pub use xdlrs::jsonc_edit;
pub mod level_model;
pub mod content_music;
pub mod content_tracks;
pub mod gizmo;
