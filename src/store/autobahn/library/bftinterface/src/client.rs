use anyhow::{Context, Result};
use bytes::BufMut as _;
use bytes::BytesMut;
use futures::sink::SinkExt as _;
use std::net::SocketAddr;
use tokio::net::TcpStream;
use tokio::runtime::Runtime;
use tokio_util::codec::{Framed, LengthDelimitedCodec};

pub struct AutobahnClient {
    // only connects to a single node
    transport: Framed<TcpStream, LengthDelimitedCodec>,
}

impl AutobahnClient {
    fn new(target: SocketAddr) -> Self {
        let rt = Runtime::new().unwrap();
        let stream = rt
            .block_on(TcpStream::connect(target))
            .expect(&format!("failed to connect to {}", target));
        let transport = Framed::new(stream, LengthDelimitedCodec::new());
        Self { transport }
    }

    pub fn send(&mut self, buf: &[u8]) -> Result<()> {
        let mut tx = BytesMut::with_capacity(buf.len());
        tx.put_slice(&buf);
        let bytes = tx.split().freeze(); //split() moves byte content from tx to bytes (i.e. avoids copy). freeze() makes it const so it can be shared. (bytes can now be used/sent async)

        let rt = Runtime::new().unwrap();
        rt.block_on(self.transport.send(bytes))
            .context("failed to send transaction")?;
        Ok(())
    }
}

// Exposed constructor
pub fn new_client(target: String) -> Box<AutobahnClient> {
    let target = target.parse().unwrap();
    Box::new(AutobahnClient::new(target))
}
