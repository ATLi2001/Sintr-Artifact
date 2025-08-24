mod client;
mod server;

#[cxx::bridge]
mod ffi {
    extern "Rust" {
        type AutobahnClient;

        fn new_client(target: String) -> Box<AutobahnClient>;
        fn send(self: &mut AutobahnClient, buf: &[u8]) -> Result<()>;

        fn start_server(
            handle: i64,
            key_file: String,
            committee_file: String,
            parameters_file: String,
            store_path: String,
            is_primary: bool,
            worker_id: u32,
        );
    }

    unsafe extern "C++" {
        include!("autobahn_callback.h");

        #[namespace = "autobahn"]
        fn autobahn_callback(handle: i64, message: String);
    }
}

pub use crate::client::{new_client, AutobahnClient};
pub use crate::server::start_server;
