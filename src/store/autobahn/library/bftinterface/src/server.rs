use bridge_debug::debug_via_cpp;
use config::Import as _;
use config::{Committee, KeyPair, Parameters};
use crypto::SignatureService;
use primary::Primary;
use store::Store;
use tokio::runtime::{Builder, Runtime};
use tokio::sync::mpsc::channel;
use tokio::task::LocalSet;
use worker::{SlotTransactionReply, Worker};

use crate::ffi::autobahn_callback;

/// The default channel capacity.
const CHANNEL_CAPACITY: usize = 1_000;

pub struct AutobahnServer {
    // keep tokio runtime alive
    rt: Runtime,
}

impl AutobahnServer {
    fn new() -> Self {
        let rt = Runtime::new().unwrap();
        Self { rt }
    }

    pub fn start_server(
        &self,
        handle: i64,
        key_file: String,
        committee_file: String,
        parameters_file: String,
        store_path: String,
        is_primary: bool,
        worker_id: u32,
    ) {
        let (tx_slot_txn_reply, mut rx_slot_txn_reply) = channel(CHANNEL_CAPACITY);

        self.rt.spawn(async move {
            debug_via_cpp("Starting Autobahn server...");
            debug_via_cpp(&format!(
                "key_file: {}, committee_file: {}, parameters_file: {}, store_path: {}, is_primary: {}, worker_id: {}",
                key_file, committee_file, parameters_file, store_path, is_primary, worker_id
            ));
            start_server_inner(
                key_file,
                committee_file,
                parameters_file,
                store_path,
                is_primary,
                worker_id,
                tx_slot_txn_reply,
            )
            .await;
        });

        // always process the slot transaction replies in a fixed thread
        std::thread::spawn(move || {
            let rt = Builder::new_current_thread().enable_all().build().unwrap();
            let local = LocalSet::new();
            local.block_on(&rt, async {
                let mut highest_committed_slot: u64 = 0;
                while let Some(slot_txn_reply) = rx_slot_txn_reply.recv().await {
                    // debug_via_cpp(&format!(
                    //     "Worker sending reply for slot {} with {:?} bytes",
                    //     slot_txn_reply.slot, slot_txn_reply.committed_transaction
                    // ));
                    autobahn_callback(
                        handle,
                        slot_txn_reply.slot,
                        &slot_txn_reply.committed_request,
                    );

                    if slot_txn_reply.slot > highest_committed_slot {
                        highest_committed_slot = slot_txn_reply.slot;
                    } else if slot_txn_reply.slot < highest_committed_slot {
                        debug_via_cpp(&format!(
                            "Warning: Worker committed slot {} but highest committed slot is {}",
                            slot_txn_reply.slot, highest_committed_slot
                        ));
                    }
                }
            });
        });
    }
}

async fn start_server_inner(
    key_file: String,
    committee_file: String,
    parameters_file: String,
    store_path: String,
    is_primary: bool,
    worker_id: u32,
    tx_slot_txn_reply: tokio::sync::mpsc::Sender<SlotTransactionReply>,
) {
    // Read the committee and node's keypair from file.
    let keypair = KeyPair::import(&key_file).expect("Failed to load the node's keypair");
    let name = keypair.name;
    let committee =
        Committee::import(&committee_file).expect("Failed to load the committee information");

    // Load default parameters if none are specified.
    let parameters =
        Parameters::import(&parameters_file).expect("Failed to load the node's parameters");

    // The `SignatureService` provides signatures on input digests.
    let signature_service = SignatureService::new(keypair.secret);

    // Make the data store.
    let store = Store::new(&store_path).expect("Failed to create a store");

    // Channels the sequence of certificates.
    let (tx_output, mut rx_output) = channel(CHANNEL_CAPACITY);

    // Channel for sending headers between DAG and Consensus
    let (tx_sailfish, _rx_sailfish) = channel(CHANNEL_CAPACITY);

    if is_primary {
        let (tx_new_certificates, _rx_new_certificates) = channel(CHANNEL_CAPACITY);
        let (_tx_feedback, rx_feedback) = channel(CHANNEL_CAPACITY);
        let (tx_committer, rx_committer) = channel(CHANNEL_CAPACITY);
        let (_tx_pushdown_cert, rx_pushdown_cert) = channel(CHANNEL_CAPACITY);
        let (_tx_request_header_sync, rx_request_header_sync) = channel(CHANNEL_CAPACITY);

        Primary::spawn(
            name,
            committee.clone(),
            parameters.clone(),
            signature_service.clone(),
            store.clone(),
            /* tx_consensus */ tx_new_certificates,
            tx_committer,
            rx_committer,
            /* rx_consensus */ rx_feedback,
            tx_sailfish,
            //rx_ticket,
            rx_pushdown_cert,
            rx_request_header_sync,
            tx_output,
        );
    } else {
        Worker::spawn(
            keypair.name,
            worker_id,
            committee,
            parameters,
            store,
            tx_slot_txn_reply,
        );
    }

    while let Some(_header) = rx_output.recv().await {
        // debug_via_cpp("Primary produced header");
        // autobahn_callback(handle, header.to_string());
    }
}

pub fn new_server() -> Box<AutobahnServer> {
    Box::new(AutobahnServer::new())
}
