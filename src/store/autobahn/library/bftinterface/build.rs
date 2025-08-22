fn main() {
    cxx_build::bridge("src/lib.rs")
        .flag_if_supported("-std=c++17")
        .compile("bftinterface");
    // .file("cppinclude/autobahn_callback.cc")
    // .include("cppinclude")

    println!("cargo:rerun-if-changed=src/lib.rs");
    // println!("cargo:rerun-if-changed=cppinclude/autobahn_callback.h");
    // println!("cargo:rerun-if-changed=cppinclude/autobahn_callback.cc");
}
