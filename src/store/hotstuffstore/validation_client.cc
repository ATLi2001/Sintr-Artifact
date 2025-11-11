/***********************************************************************
 *
 * Copyright 2024 Austin Li <atl63@cornell.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#include "store/hotstuffstore/validation_client.h"
#include "store/common/transaction.h"
#include "lib/message.h"
#include "store/common/util.h"

namespace hotstuffstore {

ValidationClient::ValidationClient(Transport *transport, uint64_t client_id, SintrParameters sintr_params, Partitioner *part, int nShards, int nGroups) : 
    transport(transport), client_id(client_id), sintr_params(sintr_params), part(part), nshards(nShards), ngroups(nGroups) {}

ValidationClient::~ValidationClient() {
  for (auto it = allValTxnStates.begin(); it != allValTxnStates.end(); ++it) {
    delete it->second;
  }
  allValTxnStates.clear();
}

void ValidationClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {

  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // Begin should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  a.release();
  bcb(txn_client_seq_num);
}

void ValidationClient::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {

  validation_read_callback vrcb = [gcb, this](int status, uint64_t txn_client_id, uint64_t txn_client_seq_num, 
      const std::string &key, const std::string &value, const Timestamp &ts) {
    
    Debug("validation_read_callback on key %s, value %s", BytesToHex(key, 16).c_str(), BytesToHex(value, 16).c_str());
    gcb(status, key, value, ts);
  };
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  std::vector<int> txnGroups;
  int i = (*part)(key, nshards, -1, txnGroups) % ngroups;

  // If needed, add this shard to set of participants and send BEGIN.
  if (!IsParticipant(i, *a->second->txn)) {
    a->second->txn->add_participating_shards(i);
  }
  a->second->seenReads.insert(key);
  PendingValidationGet *pendingGet = new PendingValidationGet(txn_client_id, txn_client_seq_num, key);

  auto itr = std::find_if(
    a->second->pendingForwardedRead.begin(), a->second->pendingForwardedRead.end(),
    [&curr_key = pendingGet->key](const auto &read_results) { return read_results.first == curr_key; }
  );
  if(itr != a->second->pendingForwardedRead.end()) {
    if(itr->second.second.getTimestamp() != -1) { // -1 timestamp means abort
      gcb(REPLY_OK, key, itr->second.first, itr->second.second);
    } else {
      gcb(REPLY_FAIL, key, "", Timestamp());
    }
    delete pendingGet;
    a->second->pendingForwardedRead.erase(itr);
    return;
  }

  Debug(
    "Registering pendingGet for client id %lu, seq num %lu on key %s",
    txn_client_id,
    txn_client_seq_num,
    BytesToHex(key, 16).c_str()
  );

  pendingGet->vrcb = vrcb;
  pendingGet->vrtcb = gtcb;


  a->second->pendingGets.push_back(pendingGet);

  pendingGet->timeout = new Timeout(transport, 2000, [this, txn_id, pendingGet]() {
    allValTxnStatesMap::accessor a;
    if (!allValTxnStates.find(a, txn_id)) {
      // transaction has completed
      return;
    }
    std::vector<PendingValidationGet *> pendingGets = a->second->pendingGets;

    auto reqs_itr = std::find_if(
      pendingGets.begin(), pendingGets.end(), 
      [curr_key = pendingGet->key](const PendingValidationGet *req) { return req->key == curr_key; }
    );
    if (reqs_itr == pendingGets.end()) {
      // pendingGet fulfilled
      return;
    }
    Panic("Timeout triggered for txn_id %s key %s", txn_id.c_str(), BytesToHex(pendingGet->key, 16).c_str());
  });

  pendingGet->timeout->Reset();
}

void ValidationClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb, uint32_t timeout) {
        uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  Debug("Validation PUT[%lu:%lu] key %s value %s", txn_client_id, txn_client_seq_num, BytesToHex(key,16).c_str(), BytesToHex(value,16).c_str());

  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // Put should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  std::vector<int> txnGroups;
  int i = (*part)(key, nshards, -1, txnGroups) % ngroups;

  // If needed, add this shard to set of participants and send BEGIN.
  if (!IsParticipant(i, *a->second->txn)) {
    a->second->txn->add_participating_shards(i);
  }


  WriteMessage *write = a->second->txn->add_writeset();
  write->set_key(key);
  write->set_value(value);
  a.release();
  pcb(REPLY_OK, key, value);
}

void ValidationClient::Commit(commit_callback ccb, commit_timeout_callback ctcb,
    uint32_t timeout) {

  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // Put should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  if (!a->second->pendingForwardedRead.empty()) {
    // TODO: remove extra readset additions from transaction and send to initiating client
    // May also trigger if validating client receives duplicated messages due to asynchrony
    for(auto const &i : a->second->pendingForwardedRead) {
      Debug("extra fwd keys: %s", BytesToHex(i.first, 16).c_str());
    }

    Panic("Transaction includes extra forwarded Read Results: %u", a->second->pendingForwardedRead.size());
  }
  std::set<std::string> readsInReadset;
  for (const auto &read : a->second->txn->readset()) {
    readsInReadset.insert(read.key());
  }
  if (readsInReadset != a->second->seenReads) {
    Panic("Transaction readset does not match the reads seen by the validating client.");
  }

  Debug("Committing validation for client id %lu, seq num %lu", txn_client_id, txn_client_seq_num);

  if (sintr_params.parallelQuerySigsCheck && a->second->numValidForwardRead < a->second->numProcessedForwardRead) {
    // still need to wait for all forward queries to be validated
    // call commit callback later
    Debug(
      "Validating for client id %lu seq num %lu commit received %lu of %lu parallel read sigs check",
      txn_client_id,
      txn_client_seq_num,
      a->second->numValidForwardRead,
      a->second->numProcessedForwardRead
    );
    a->second->commitWaitOnValidForwardRead = true;
    a->second->ccb = std::move(ccb);
    a->second->ctcb = std::move(ctcb);
    return;
  }

  ccb(COMMITTED);
}

void ValidationClient::Abort(abort_callback acb, abort_timeout_callback atcb,
    uint32_t timeout) {
  // on abort, clean up stored data
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // Abort should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  Debug("Validation ABORT[%lu:%lu]", txn_client_id, txn_client_seq_num);

  allValTxnStates.erase(a);
  a.release();
  acb();
}

void ValidationClient::ProcessForwardReadResult(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    proto::ForwardReadResult &&fwdReadResult) {
  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();
  Timestamp curr_ts = Timestamp(fwdReadResult.timestamp());
  Debug(
    "ProcessForwardReadResult from client id %lu, seq num %lu, key %s, value %s", 
    txn_client_id, 
    txn_client_seq_num,
    BytesToHex(curr_key, 16).c_str(),
    BytesToHex(curr_value, 16).c_str()
  );

  // lambda for editing txn state
  auto editTxnStateCB = [&curr_key, &curr_value, &curr_ts](AllValidationTxnState *allValTxnState) {
    ++allValTxnState->numProcessedForwardRead;
    ReadMessage *read = allValTxnState->txn->add_readset();
    read->set_key(curr_key);
    curr_ts.serialize(read->mutable_readtime());
  };

  // find matching pending read by first going off txn client id and sequence number
  // if forwarded read result is for a read that the validation transaction has not yet gotten to,
  // add it to the appropriate transaction read result cache

  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  const bool isNewKey = allValTxnStates.insert(a, curr_txn_id);
  if (isNewKey) {
    Debug(
      "ProcessForwardReadResult from client id %lu, seq num %lu, before txn_id in allValTxnStates registered for key %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(curr_key, 16).c_str()
    );
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num);
    editTxnStateCB(a->second);
    a->second->pendingForwardedRead.push_back(std::make_pair(curr_key, std::make_pair(curr_value, curr_ts)));
    return;
  }

  std::vector<PendingValidationGet *> *reqs = &a->second->pendingGets;
  auto reqs_itr = std::find_if(
    reqs->begin(), reqs->end(), 
    [&curr_key](const PendingValidationGet *req) { return req->key == curr_key; }
  );
  if (reqs_itr == reqs->end()) {
    Debug(
      "ProcessForwardReadResult from client id %lu, seq num %lu, before PendingGet registered for key %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(curr_key, 16).c_str()
    );
    a->second->pendingForwardedRead.push_back(std::make_pair(curr_key, std::make_pair(curr_value, curr_ts)));
    editTxnStateCB(a->second);
    return;
  }

  // callback
  PendingValidationGet *req = *reqs_itr;

  req->ts = curr_ts;
  editTxnStateCB(a->second);
  req->vrcb(curr_ts.getTimestamp() == -1 ? REPLY_FAIL : REPLY_OK, txn_client_id, txn_client_seq_num, req->key, curr_value, curr_ts.getTimestamp() == -1 ? Timestamp() : req->ts);

  // remove from vector
  reqs->erase(reqs_itr);
  // free memory
  delete req;
}

void ValidationClient::NotifyForwardReadResultValid(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, curr_txn_id)) {
    Debug("cannot find transaction %s in allValTxnStates, transaction may have been aborted", curr_txn_id.c_str());
    // Just debug because it's possible txn aborts before read sigs are all validated...
    return;
  }
  ++a->second->numValidForwardRead;
  Debug(
    "numValidForwardRead for client id %lu seq num %lu is now %lu",
    txn_client_id, txn_client_seq_num, a->second->numValidForwardRead
  );
  if (a->second->commitWaitOnValidForwardRead && a->second->numValidForwardRead == a->second->numProcessedForwardRead) {
    Debug("Unblocking commit for client id %lu seq num %lu", txn_client_id, txn_client_seq_num);
    a->second->commitWaitOnValidForwardRead = false;
    a->second->ccb(COMMITTED);
  }
}

std::unique_ptr<proto::Transaction> ValidationClient::GetCompletedTxn(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // GetCompletedTxn is called after validation has completed
    // so txn_id must be in allValTxnStates
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }
  std::unique_ptr<proto::Transaction> txn = std::move(a->second->txn);

  Debug(
    "ValidationClient::GetCompletedValTxn called for txn client id %lu, seq num %lu",
    txn_client_id,
    txn_client_seq_num
  );

  allValTxnStates.erase(a);
  return txn;
}

void ValidationClient::SetTxnTimestamp(uint64_t txn_client_id, uint64_t txn_client_seq_num, const Timestamp &ts) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  const bool isNewKey = allValTxnStates.insert(a, txn_id);
  if (isNewKey) {
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num);
    ts.serialize(a->second->txn->mutable_timestamp());
  } 
}

bool ValidationClient::IsParticipant(int g, const proto::Transaction &txn) {
  for (const auto &participant : txn.participating_shards()) {
    if (participant == (uint64_t) g) {
      return true;
    }
  }
  return false;
}

} // namespace pelotonstore
