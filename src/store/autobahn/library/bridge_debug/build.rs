fn main() {
    cxx_build::bridge("src/lib.rs")
        .file("cppinclude/autobahn_debug.cc")
        .include("cppinclude")
        .flag_if_supported("-std=c++17")
        .compile("bridge_debug");

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=cppinclude/autobahn_debug.h");
    println!("cargo:rerun-if-changed=cppinclude/autobahn_debug.cc");
}
