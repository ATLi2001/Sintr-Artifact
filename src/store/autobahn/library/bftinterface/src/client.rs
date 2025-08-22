use anyhow::{Context, Result};
use bytes::BufMut as _;
use bytes::BytesMut;
use futures::sink::SinkExt as _;
use tokio::runtime::Runtime;
use std::net::SocketAddr;
use tokio::net::TcpStream;
use tokio_util::codec::{Framed, LengthDelimitedCodec};

pub struct Client {
    // specifies the bit size of transactions
    size: usize,
    // only connects to a single node
    transport: Framed<TcpStream, LengthDelimitedCodec>,
}

impl Client {
    fn new(target: SocketAddr, size: usize) -> Self {
        let rt = Runtime::new().unwrap();
        let stream = rt.block_on(TcpStream::connect(target))
            .expect(&format!("failed to connect to {}", target));
        let transport = Framed::new(stream, LengthDelimitedCodec::new());
        Self {
            size,
            transport,
        }
    }

    pub fn send(&mut self, buf: Vec<u8>) -> Result<()> {
        let mut tx = BytesMut::with_capacity(self.size);
        tx.put_u8(1u8); // Standard txs start with 1.
        tx.put_slice(&buf);
        tx.resize(self.size, 0u8); //Truncate any bits past size
        let bytes = tx.split().freeze(); //split() moves byte content from tx to bytes (i.e. avoids copy). freeze() makes it const so it can be shared. (bytes can now be used/sent async)

        let rt = Runtime::new().unwrap();
        rt.block_on(self.transport.send(bytes)).context("failed to send transaction")?;
        Ok(())
    }
}

// Exposed constructor
pub fn new_client(target: String, size: usize) -> Box<Client> {
    let target = target.parse().unwrap();
    Box::new(Client::new(target, size))
}
