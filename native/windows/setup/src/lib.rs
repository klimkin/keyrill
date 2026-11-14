//! Verified Windows release contents and recoverable installation ownership.
#![deny(unsafe_op_in_unsafe_fn)]

pub mod journal;
#[cfg(windows)]
pub mod native;
pub mod package;

/// Content-free operation failure.
pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
