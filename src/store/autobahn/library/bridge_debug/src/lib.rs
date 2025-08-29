#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("autobahn_debug.h");

        #[namespace = "autobahn"]
        fn debug_via_cpp(message: &str);
    }
}

pub fn debug_via_cpp(message: &str) {
    ffi::debug_via_cpp(message);
}
