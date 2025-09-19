use crate::worker::{SlotTransactionReply, WorkerMessage};
use crypto::Digest;
use log::debug;
use std::collections::{BTreeMap, HashMap};
use std::convert::TryInto;
use store::Store;
use tokio::sync::mpsc::{Receiver, Sender};

/// The ReplySender component receives SlotCommittedMessage notifications from the primary
/// and sends transaction replies to clients using the same connection pattern that clients
/// use to send transactions to workers.
pub struct ReplySender {
    /// Receiver for slot committed messages containing (slot, batch_digests).
    rx_slot_committed: Receiver<(u64, Vec<Digest>)>,
    /// The persistent storage for reading batches.
    store: Store,
    /// Sender to bftinterface for slot transaction replies.
    tx_slot_txn_reply: Sender<SlotTransactionReply>,
}

impl ReplySender {
    pub fn spawn(
        rx_slot_committed: Receiver<(u64, Vec<Digest>)>,
        store: Store,
        tx_slot_txn_reply: Sender<SlotTransactionReply>,
    ) {
        tokio::spawn(async move {
            Self {
                rx_slot_committed,
                store,
                tx_slot_txn_reply,
            }
            .run()
            .await;
        });
    }

    async fn run(&mut self) {
        while let Some((slot, batch_digests)) = self.rx_slot_committed.recv().await {
            debug!(
                "ReplySender: received slot {} with {} batch digests",
                slot,
                batch_digests.len()
            );

            // Group requests by client_id and then by client_seq_num sorted in ascending order
            let mut client_transactions: HashMap<u8, BTreeMap<u64, Vec<Vec<u8>>>> = HashMap::new();

            // Read batches from store and extract transaction IDs
            for digest in batch_digests {
                match self.store.read(digest.to_vec()).await {
                    Ok(Some(serialized_batch)) => {
                        // Deserialize the WorkerMessage
                        match bincode::deserialize::<WorkerMessage>(&serialized_batch) {
                            Ok(WorkerMessage::Batch(batch)) => {
                                for transaction in batch {
                                    let client_id = transaction[0];
                                    let client_seq_num =
                                        u64::from_be_bytes(transaction[1..9].try_into().unwrap());
                                    client_transactions
                                        .entry(client_id)
                                        .or_insert_with(BTreeMap::new)
                                        .entry(client_seq_num)
                                        .or_insert_with(Vec::new)
                                        .push(transaction[9..].to_vec());
                                }
                            }
                            Ok(_) => {
                                debug!("ReplySender: unexpected message type in batch store for digest {:?}", digest);
                            }
                            Err(e) => {
                                debug!(
                                    "ReplySender: failed to deserialize batch for digest {:?}: {}",
                                    digest, e
                                );
                            }
                        }
                    }
                    Ok(None) => {
                        debug!("ReplySender: batch digest {:?} not found in store", digest);
                    }
                    Err(e) => {
                        debug!(
                            "ReplySender: error reading batch from store for digest {:?}: {}",
                            digest, e
                        );
                    }
                }
            }

            // send replies so that each client gets seq_nums in ascending order
            for (client_id, curr_client_transactions) in client_transactions {
                for (client_seq_num, requests) in curr_client_transactions {
                    for request in requests {
                        // debug_via_cpp(&format!(
                        //     "ReplySender: sending reply for slot {} for client id {}, seq num {} with {} bytes",
                        //     slot, _client_id, _client_seq_num, request.len()
                        // ));
                        let reply = SlotTransactionReply {
                            slot,
                            client_id,
                            client_seq_num,
                            committed_request: request.clone(),
                        };

                        if let Err(e) = self.tx_slot_txn_reply.send(reply).await {
                            debug!("Failed to send reply: {}", e);
                        }
                    }
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use store::Store;
    use tokio::sync::mpsc;

    #[tokio::test]
    async fn test_reply_sender_creation() {
        let (_tx, rx) = mpsc::channel(10);
        let store_path = ".db_test_reply_sender";
        let store = Store::new(store_path).unwrap();

        let (tx_slot_txn_reply, _rx_slot_txn_reply) = mpsc::channel(10);

        // This should not panic
        ReplySender::spawn(rx, store, tx_slot_txn_reply);

        // Cleanup
        let _ = std::fs::remove_dir_all(store_path);
    }
}
