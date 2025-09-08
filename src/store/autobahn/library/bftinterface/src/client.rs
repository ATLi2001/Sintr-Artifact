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
    // client id
    client_id: u8,
    // keep tokio runtime alive
    rt: Runtime,
    // only connects to a single node
    transport: Framed<TcpStream, LengthDelimitedCodec>,
}

impl AutobahnClient {
    fn new(client_id: u8, target: SocketAddr) -> Self {
        let rt = Builder::new_current_thread().enable_all().build().unwrap();
        let stream = rt
            .block_on(TcpStream::connect(target))
            .expect(&format!("failed to connect to {}", target));
        let transport = Framed::new(stream, LengthDelimitedCodec::new());
        Self {
            client_id,
            rt,
            transport,
        }
    }

    pub fn send(&mut self, client_seq_num: u64, buf: &[u8]) -> Result<()> {
        let mut tx = BytesMut::with_capacity(buf.len() + 9);
        tx.put_u8(self.client_id);
        tx.put_u64(client_seq_num);
        tx.put_slice(buf);
        let bytes = tx.split().freeze(); //split() moves byte content from tx to bytes (i.e. avoids copy). freeze() makes it const so it can be shared. (bytes can now be used/sent async)

        let transport = &mut self.transport;
        self.rt.block_on(async move {
            // debug_via_cpp(&format!("Sending transaction of size: {}", bytes.len()));
            if let Err(e) = transport.send(bytes).await {
                debug_via_cpp(&format!("Error sending transaction: {:?}", e));
            }
        });
        Ok(())
    }
}

// Exposed constructor
pub fn new_client(client_id: u8, target: String) -> Box<AutobahnClient> {
    let target = target.parse().unwrap();
    debug_via_cpp(&format!(
        "Creating new Autobahn client for target: {}",
        target
    ));
    Box::new(AutobahnClient::new(client_id, target))
}
