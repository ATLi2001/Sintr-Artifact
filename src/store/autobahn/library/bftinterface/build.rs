fn main() {
    cxx_build::bridge("src/lib.rs")
        .file("cppinclude/autobahn_callback.cc")
        .include("cppinclude")
        .include("../../../..") // include the root directory src/ so we can include lib/...
        .flag_if_supported("-std=c++17")
        .compile("bftinterface");

    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=cppinclude/autobahn_callback.h");
    println!("cargo:rerun-if-changed=cppinclude/autobahn_callback.cc");
}
