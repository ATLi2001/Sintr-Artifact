use anyhow::Result;
use bridge_debug::debug_via_cpp;
use bytes::BufMut as _;
use bytes::BytesMut;
use futures::sink::SinkExt as _;
use std::net::SocketAddr;
use tokio::net::TcpStream;
use tokio::runtime::{Builder, Runtime};
use tokio_util::codec::{Framed, LengthDelimitedCodec};

pub struct AutobahnClient {
    // keep tokio runtime alive
    rt: Runtime,
    // only connects to a single node
    transport: Framed<TcpStream, LengthDelimitedCodec>,
}

impl AutobahnClient {
    fn new(target: SocketAddr) -> Self {
        let rt = Builder::new_current_thread().enable_all().build().unwrap();
        let stream = rt
            .block_on(TcpStream::connect(target))
            .expect(&format!("failed to connect to {}", target));
        let transport = Framed::new(stream, LengthDelimitedCodec::new());
        Self { rt, transport }
    }

    pub fn send(&mut self, buf: &[u8]) -> Result<()> {
        let mut tx = BytesMut::with_capacity(buf.len());
        tx.put_slice(buf);
        let bytes = tx.split().freeze(); //split() moves byte content from tx to bytes (i.e. avoids copy). freeze() makes it const so it can be shared. (bytes can now be used/sent async)

        let transport = &mut self.transport;
        self.rt.block_on(async move {
            debug_via_cpp(&format!("Sending transaction of size: {}", bytes.len()));
            if let Err(e) = transport.send(bytes).await {
                debug_via_cpp(&format!("Error sending transaction: {:?}", e));
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
