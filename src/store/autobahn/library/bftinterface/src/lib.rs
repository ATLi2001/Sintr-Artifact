mod client;
mod server;

#[cxx::bridge]
mod ffi {
    extern "Rust" {
        type AutobahnClient;
        fn new_client(target: String) -> Box<AutobahnClient>;
        fn send(self: &mut AutobahnClient, buf: &[u8]) -> Result<()>;

        type AutobahnServer;
        fn new_server() -> Box<AutobahnServer>;
        fn start_server(
            self: &AutobahnServer,
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
        fn autobahn_callback(handle: i64, slot_num: u64, buf: &[u8]);
    }
}

pub use crate::client::{new_client, AutobahnClient};
pub use crate::server::{new_server, AutobahnServer};
