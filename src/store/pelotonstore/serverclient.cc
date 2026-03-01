/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Yunhao Zhang <yz2327@cornell.edu>
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
#include "store/pelotonstore/serverclient.h"
#include "store/pelotonstore/common.h"
#include "store/common/transaction.h"
#include "store/common/query_result/query_result_proto_wrapper.h"

namespace pelotonstore {

using namespace std;

ServerClient::ServerClient(Server *server, uint64_t id, Transport *transport,
    TrueTime timeserver)
    : server(server), client_id(id), transport(transport),
      timeServer(timeserver), client_seq_num(0) {
  Debug("Initializing pelotonstore ServerClient with id [%lu]", client_id);
}

ServerClient::~ServerClient() {
  Debug("pelotonstore ServerClient deleted!");
}

// All methods call their callbacks synchronously (no transport->Timer) because:
//  - Direct* calls on Server are synchronous
//  - SyncClient wraps this class and blocks on a Promise::GetReply() after each
//    call, so the callback must fire before returning or the caller deadlocks

void ServerClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {
  Debug("BEGIN tx");
  client_seq_num++;
  server->DirectBegin(client_id, client_seq_num);
  bcb(client_seq_num);
}

void ServerClient::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {
  // Get is not used for SQL benchmarks; panic to catch misuse.
  Panic("pelotonstore::ServerClient::Get should not be called for SQL benchmarks");
}

void ServerClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb, uint32_t timeout) {
  // Put is not used for SQL benchmarks; panic to catch misuse.
  Panic("pelotonstore::ServerClient::Put should not be called for SQL benchmarks");
}

void ServerClient::Query(const std::string &query_statement, query_callback qcb,
    query_timeout_callback qtcb, uint32_t timeout,
    bool cache_result, bool skip_query_interpretation) {
  Debug("QUERY [%s]", query_statement.c_str());

  int status;
  std::string result = server->DirectExecSQL(query_statement, client_id,
      client_seq_num, status);

  // Build a QueryResult from the serialised result string (same as the normal
  // client path through HandleSQL_RPC).
  query_result::QueryResult *qr = nullptr;
  if (status == REPLY_OK && !result.empty()) {
    qr = new sql::QueryResultProtoWrapper(result);
  } else {
    qr = new sql::QueryResultProtoWrapper();
  }
  qcb(status, qr);
}

void ServerClient::Write(std::string &write_statement, write_callback wcb,
    write_timeout_callback wtcb, uint32_t timeout, bool blind_write) {
  Debug("WRITE [%s]", write_statement.c_str());

  int status;
  std::string result = server->DirectExecSQL(write_statement, client_id,
      client_seq_num, status);

  // Build a QueryResult from the serialised result (rows affected, etc.).
  query_result::QueryResult *qr = nullptr;
  if (status == REPLY_OK && !result.empty()) {
    qr = new sql::QueryResultProtoWrapper(result);
  } else {
    qr = new sql::QueryResultProtoWrapper();
  }
  wcb(status, qr);
}

void ServerClient::Commit(commit_callback ccb, commit_timeout_callback ctcb,
    uint32_t timeout) {
  Debug("COMMIT tx");
  int status = server->DirectCommit(client_id, client_seq_num);
  transaction_status_t txn_status = (status == REPLY_OK) ? COMMITTED : ABORTED_SYSTEM;
  ccb(txn_status);
}

void ServerClient::Abort(abort_callback acb, abort_timeout_callback atcb,
    uint32_t timeout) {
  Debug("ABORT tx");
  server->DirectAbort(client_id, client_seq_num);
  acb();
}

} // namespace pelotonstore
