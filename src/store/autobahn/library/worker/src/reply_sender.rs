// Copyright(C) Facebook, Inc. and its affiliates.
use crate::{
    batch_maker::Transaction,
    worker::{SlotTransactionReply, WorkerMessage},
};
use crypto::Digest;
use log::debug;
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

            // Group transactions by client_id
            let mut client_transactions: Vec<Transaction> = Vec::new();

            // Read batches from store and extract transaction IDs
            for digest in batch_digests {
                match self.store.read(digest.to_vec()).await {
                    Ok(Some(serialized_batch)) => {
                        // Deserialize the WorkerMessage
                        match bincode::deserialize::<WorkerMessage>(&serialized_batch) {
                            Ok(WorkerMessage::Batch(batch)) => {
                                for transaction in batch {
                                    client_transactions.push(transaction);
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

            // Send batched replies to each client using persistent connections
            for transaction in client_transactions {
                let reply = SlotTransactionReply {
                    slot,
                    committed_transaction: transaction,
                };

                if let Err(e) = self.tx_slot_txn_reply.send(reply).await {
                    debug!("Failed to send reply: {}", e);
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
