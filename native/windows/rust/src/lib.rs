//! Windows TSF adapter with Rust-owned native authority.
#![deny(unsafe_op_in_unsafe_fn)]
#![cfg_attr(windows, allow(non_snake_case))]

#[cfg(windows)]
mod com;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "Journal callers transfer with the native executor and command unit."
)]
mod diagnostics;
#[cfg(windows)]
mod engine;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "Publication callers transfer with the native executor unit."
)]
mod observation;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "The shipping diagnostics path consumes this identity during migration."
)]
mod build_identity {
    include!(concat!(env!("OUT_DIR"), "/build_identity.rs"));
}
#[cfg(all(windows, feature = "test-api"))]
mod edit;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "Native field admission is exercised through the test DLL while callers migrate."
)]
mod field;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "Frozen native-field transport precedes admission implementation."
)]
mod field_contract;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "The test DLL exercises this foundation while native callers migrate."
)]
mod foreign;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "Native key behavior is exercised through the test DLL while callers migrate."
)]
mod keys;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "Registration and activation consume the shared profile metadata during migration."
)]
mod profiles;
#[cfg(windows)]
pub mod registrar;
#[cfg(windows)]
mod service;
#[cfg(all(windows, feature = "test-api"))]
mod test_api;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "The controlled worker schedules run this implementation while service callers migrate."
)]
mod vocabulary;
#[cfg(windows)]
#[allow(
    dead_code,
    reason = "Frozen service/worker contract precedes worker implementation."
)]
mod vocabulary_contract;

#[cfg(windows)]
pub use com::{DllCanUnloadNow, DllGetClassObject};
