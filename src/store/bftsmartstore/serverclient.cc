/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Zheng Wang <zw494@cornell.edu>
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
#include "store/bftsmartstore/serverclient.h"
#include "store/bftsmartstore/common.h"
#include "store/common/transaction.h"

namespace bftsmartstore {

using namespace std;

ServerClient::ServerClient(Server *server, uint64_t id, Transport *transport,
    TrueTime timeserver)
    : server(server), client_id(id), transport(transport),
      timeServer(timeserver), client_seq_num(0), read_req_id(0) {
  Debug("Initializing ServerClient with id [%lu]", client_id);
}

ServerClient::~ServerClient() {
  Debug("ServerClient deleted!");
}

// All methods call their callbacks synchronously (no transport->Timer) because:
//  - DirectRead/DirectCommit are themselves synchronous
//  - SyncClient wraps this class and blocks on a Promise::GetReply() after each
//    call, so the callback must fire before returning or the caller deadlocks

void ServerClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {
  Debug("BEGIN tx");
  client_seq_num++;
  currentTxn = proto::Transaction();
  currentTxn.mutable_timestamp()->set_timestamp(timeServer.GetTime());
  currentTxn.mutable_timestamp()->set_id(client_id);
  bcb(client_seq_num);
}

void ServerClient::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {
  Debug("GET [%s]", key.c_str());

  uint64_t req_id = ++read_req_id;
  Timestamp ts(currentTxn.timestamp());

  // DirectRead calls gcb synchronously from within this call
  server->DirectRead(key, ts, req_id,
      [this, gcb](int status, const std::string &k,
          const std::string &val, const Timestamp &valTs) {
        if (status == REPLY_OK) {
          ReadMessage *read = currentTxn.add_readset();
          read->set_key(k);
          valTs.serialize(read->mutable_readtime());
        }
        gcb(status, k, val, valTs);
      });
}

void ServerClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb, uint32_t timeout) {
  // Buffer write locally; no network call needed
  WriteMessage *write = currentTxn.add_writeset();
  write->set_key(key);
  write->set_value(value);
  pcb(REPLY_OK, key, value);
}

void ServerClient::Commit(commit_callback ccb, commit_timeout_callback ctcb,
    uint32_t timeout) {
  Debug("COMMIT tx");
  currentTxn.set_client_id(client_id);
  transaction_status_t status = server->DirectCommit(currentTxn);
  ccb(status);
}

void ServerClient::Abort(abort_callback acb, abort_timeout_callback atcb,
    uint32_t timeout) {
  AbortTxn(currentTxn);
  acb();
}

void ServerClient::AbortTxn(const proto::Transaction &txn) {
  // For a server-side client, abort is a no-op since the transaction was
  // never sent through the normal shard prepare path.
  Debug("AbortTxn: no shard state to clean up for ServerClient");
}

} // namespace bftsmartstore
