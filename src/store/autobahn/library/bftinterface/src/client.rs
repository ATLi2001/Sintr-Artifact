use anyhow::Result;
use bytes::BufMut as _;
use bytes::BytesMut;
use futures::sink::SinkExt as _;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::net::TcpStream;
use tokio::runtime::Runtime;
use tokio::sync::Mutex;
use tokio_util::codec::{Framed, LengthDelimitedCodec};

use crate::ffi::debug_via_cpp;

pub struct AutobahnClient {
    // keep tokio runtime alive
    rt: Runtime,
    // only connects to a single node
    transport: Arc<Mutex<Framed<TcpStream, LengthDelimitedCodec>>>,
}

impl AutobahnClient {
    fn new(target: SocketAddr) -> Self {
        let rt = Runtime::new().unwrap();
        let stream = rt
            .block_on(TcpStream::connect(target))
            .expect(&format!("failed to connect to {}", target));
        let transport = Framed::new(stream, LengthDelimitedCodec::new());
        Self {
            rt,
            transport: Arc::new(Mutex::new(transport)),
        }
    }

    pub fn send(&mut self, buf: &[u8]) -> Result<()> {
        let mut tx = BytesMut::with_capacity(buf.len());
        tx.put_slice(&buf);
        let bytes = tx.split().freeze(); //split() moves byte content from tx to bytes (i.e. avoids copy). freeze() makes it const so it can be shared. (bytes can now be used/sent async)

        let transport_clone = Arc::clone(&self.transport);

        self.rt.spawn(async move {
            debug_via_cpp(&format!("Sending transaction of size: {}", bytes.len()));
            let mut locked = transport_clone.lock().await;
            let res = locked.send(bytes).await;
            if res.is_err() {
                debug_via_cpp(&format!("Error sending transaction: {:?}", res));
            }
        });
        Ok(())
    }
}

// Exposed constructor
pub fn new_client(target: String) -> Box<AutobahnClient> {
    let target = target.parse().unwrap();
    debug_via_cpp(&format!(
        "Creating new Autobahn client for target: {}",
        target
    ));
    Box::new(AutobahnClient::new(target))
}
