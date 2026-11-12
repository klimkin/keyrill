//! Compile the mechanical C++ foreign-exception boundary on Windows.

fn main() {
    println!("cargo:rerun-if-changed=foreign_calls.cpp");
    println!("cargo:rerun-if-changed=foreign_calls.h");
    println!("cargo:rerun-if-changed=registrar_calls.cpp");
    println!("cargo:rerun-if-changed=registrar_calls.h");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }
    println!("cargo:rustc-cdylib-link-arg=/EXPORT:DllGetClassObject,PRIVATE");
    println!("cargo:rustc-cdylib-link-arg=/EXPORT:DllCanUnloadNow,PRIVATE");
    cc::Build::new()
        .cpp(true)
        .file("foreign_calls.cpp")
        .file("registrar_calls.cpp")
        .flag("/std:c++20")
        .flag("/EHsc")
        .warnings_into_errors(true)
        .compile("keyrill_foreign_calls");
}
