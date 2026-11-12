//! The HWVX data browser: an egui front end for HWVX-SDL's viewer mode.
pub mod client;
pub mod frames;
pub mod media;
pub mod shm;
pub mod testdrive;
/// moved to the xdlrs crate (tools/xdlrs), kept here under its old path
pub use xdlrs::jsonc_edit;
pub mod level_model;
