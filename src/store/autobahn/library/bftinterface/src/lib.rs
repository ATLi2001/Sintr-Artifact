mod client;
mod server;

#[cxx::bridge]
mod ffi {
    extern "Rust" {
        type Client;

        fn new_client(target: String, size: usize) -> Box<Client>;
        fn send(self: &mut Client, buf: Vec<u8>) -> Result<()>;

        type Server;
        fn new_server(
            handle: i64,
            key_file: String,
            committee_file: String,
            parameters_file: String,
            store_path: String,
            worker_id: u32,
        ) -> Box<Server>;
    }

    unsafe extern "C++" {
        include!("autobahn_callback.h");

        #[namespace = "autobahn"]
        fn autobahn_callback(handle: i64, message: String);
    }
}

pub use crate::client::{new_client, Client};
pub use crate::server::{new_server, Server};
