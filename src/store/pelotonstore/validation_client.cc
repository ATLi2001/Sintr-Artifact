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

#include "store/pelotonstore/validation_client.h"
#include "store/common/transaction.h"
#include "lib/message.h"
#include "store/common/util.h"
#include "store/common/query_result/query_result.h"
#include "store/common/query_result/query_result_proto_wrapper.h"
#include "store/common/query_result/query_result_proto_builder.h"


namespace pelotonstore {

ValidationClient::ValidationClient(Transport *transport, uint64_t client_id, SintrParameters sintr_params) : 
    transport(transport), client_id(client_id), sintr_params(sintr_params) {}

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
  Panic("Client GET is not supported.");
}

void ValidationClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb, uint32_t timeout) {
  Panic("Client PUT is not supported.");
}

void ValidationClient::SQLRequest(std::string &statement, sql_callback scb,
    sql_timeout_callback stcb, uint32_t timeout){

  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  PendingValidationSQLRequest *pendingSQLReq = new PendingValidationSQLRequest(statement, scb, stcb,
    txn_client_id, txn_client_seq_num, sintr_params.hashQueryGenId);

  auto itr = std::find_if(
    a->second->pendingForwardedSQLResults.begin(), a->second->pendingForwardedSQLResults.end(),
    [&curr_sql_gen_id = pendingSQLReq->sql_gen_id](const auto &sql_id_result) { return sql_id_result.first == curr_sql_gen_id; }
  );
  if(itr != a->second->pendingForwardedSQLResults.end()) {
    sql::QueryResultProtoWrapper* res = new sql::QueryResultProtoWrapper(itr->second);
    scb(REPLY_OK, res);
    delete pendingSQLReq;
    a->second->pendingForwardedSQLResults.erase(itr);
    return;
  }

  Debug(
    "Registering PendingSQLRequest for client id %lu, seq num %lu on statement %s",
    txn_client_id,
    txn_client_seq_num,
    statement.c_str()
  );

  a->second->pendingSQLRequests.push_back(pendingSQLReq);

  pendingSQLReq->timeout = new Timeout(transport, 2000, [this, txn_id, pendingSQLReq]() {
    allValTxnStatesMap::accessor a;
    if (!allValTxnStates.find(a, txn_id)) {
      // transaction has completed
      return;
    }
    std::vector<PendingValidationSQLRequest *> pendingSQLRequests = a->second->pendingSQLRequests;

    auto reqs_itr = std::find_if(
      pendingSQLRequests.begin(), pendingSQLRequests.end(),
      [&curr_sql_gen_id = pendingSQLReq->sql_gen_id](const PendingValidationSQLRequest *req) { return req->sql_gen_id == curr_sql_gen_id; }
    );
    if (reqs_itr == pendingSQLRequests.end()) {
      // pendingSQLRequest fulfilled
      return;
    }

    Panic("Timeout triggered for txn_id %s statement %s", txn_id.c_str(),pendingSQLReq->statement.c_str());
  });

  pendingSQLReq->timeout->Reset();
}

void ValidationClient::Write(std::string &write_statement, write_callback wcb,
    write_timeout_callback wtcb, uint32_t timeout, bool blind_write){ //blind_write: default false, must be explicit application choice to skip.
  Debug("Processing Write Statement: %s", write_statement.c_str());
  this->SQLRequest(write_statement, wcb, wtcb, timeout);
}

void ValidationClient::Query(const std::string &query, query_callback qcb,
    query_timeout_callback qtcb, uint32_t timeout, bool cache_result, bool skip_query_interpretation) {
  Debug("Processing Query Statement: %s", query.c_str());
  this->SQLRequest(const_cast<std::string &>(query), qcb, qtcb, timeout);
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

  if (!a->second->pendingForwardedSQLResults.empty()) {
    // TODO: remove extra readset additions from transaction and send to initiating client
    // May also trigger if validating client receives duplicated messages due to asynchrony
    for(auto const &i : a->second->pendingForwardedSQLResults) {
      Debug("extra fwd SQL result ID: %s", BytesToHex(i.first, 16).c_str());
    }

    Panic("Transaction includes extra forwarded SQL Results: %u", a->second->pendingForwardedSQLResults.size());
  }

  Debug("Committing validation for client id %lu, seq num %lu", txn_client_id, txn_client_seq_num);

  if (sintr_params.parallelQuerySigsCheck && a->second->numValidForwardQuery < a->second->numProcessedForwardQuery) {
    // still need to wait for all forward queries to be validated
    // call commit callback later
    Debug(
      "Validating for client id %lu seq num %lu commit received %lu of %lu parallel query sigs check",
      txn_client_id,
      txn_client_seq_num,
      a->second->numValidForwardQuery,
      a->second->numProcessedForwardQuery
    );
    a->second->commitWaitOnValidForwardQuery = true;
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

void ValidationClient::ProcessForwardSQLResult(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    proto::ForwardSQLResult &&fwdSQLResult) {
  std::string curr_sql_gen_id = fwdSQLResult.sql_gen_id();
  std::string curr_sql_result = fwdSQLResult.sql_result();
  Debug(
    "ProcessForwardSQLResult from client id %lu, seq num %lu, sql gen id %s, sql result %s", 
    txn_client_id, 
    txn_client_seq_num,
    BytesToHex(curr_sql_gen_id, 16).c_str(),
    BytesToHex(curr_sql_result, 16).c_str()
  );

  // lambda for editing txn state
  auto editTxnStateCB = [](AllValidationTxnState *allValTxnState, proto::ForwardSQLResult &&fwdSQLResult) {
    ++allValTxnState->numProcessedForwardQuery;
    for (auto &read : fwdSQLResult.mutable_txn_msg()->readset()) {
      Debug("read key: %s", read.key().c_str());
      *allValTxnState->txn_msg->add_readset() = std::move(read);
    }
    for (auto &write : fwdSQLResult.mutable_txn_msg()->writeset()) {
      Debug("write key: %s", write.key().c_str());
      *allValTxnState->txn_msg->add_writeset() = std::move(write);
    }
  };

  // find matching pending query by first going off txn client id and sequence number, then sql_gen_id
  // if forwarded sql result is for a sql that the validation transaction has not yet gotten to,
  // add it to the appropriate transaction sql result cache

  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  const bool isNewKey = allValTxnStates.insert(a, curr_txn_id);
  if (isNewKey) {
    Debug(
      "ProcessForwardQueryResult from client id %lu, seq num %lu, before txn_id in allValTxnStates registered for query %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(curr_sql_gen_id, 16).c_str()
    );
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num);
    editTxnStateCB(a->second, std::move(fwdSQLResult));
    a->second->pendingForwardedSQLResults.push_back(std::make_pair(curr_sql_gen_id, curr_sql_result));
    return;
  }

  std::vector<PendingValidationSQLRequest *> *reqs = &a->second->pendingSQLRequests;
  auto reqs_itr = std::find_if(
    reqs->begin(), reqs->end(), 
    [&curr_sql_gen_id](const PendingValidationSQLRequest *req) { return req->sql_gen_id == curr_sql_gen_id; }
  );
  if (reqs_itr == reqs->end()) {
    Debug(
      "ProcessForwardQueryResult from client id %lu, seq num %lu, before PendingQuery registered for query %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(curr_sql_gen_id, 16).c_str()
    );
    editTxnStateCB(a->second, std::move(fwdSQLResult));
    a->second->pendingForwardedSQLResults.push_back(std::make_pair(curr_sql_gen_id, curr_sql_result));
    return;
  }

  // callback
  PendingValidationSQLRequest *req = *reqs_itr;

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // query_fin_us = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;

  sql::QueryResultProtoWrapper *q_result = new sql::QueryResultProtoWrapper(curr_sql_result);
  editTxnStateCB(a->second, std::move(fwdSQLResult));
  req->vscb(REPLY_OK, q_result);
  // no need to delete q_result since the query callback will take care of it

  // remove from vector
  reqs->erase(reqs_itr);
  // free memory
  delete req;
}

void ValidationClient::NotifyForwardQueryResultValid(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, curr_txn_id)) {
    Panic("cannot find transaction %s in allValTxnStates", curr_txn_id.c_str());
  }
  ++a->second->numValidForwardQuery;
  Debug(
    "numValidForwardQuery for client id %lu seq num %lu is now %lu",
    txn_client_id, txn_client_seq_num, a->second->numValidForwardQuery
  );
  if (a->second->commitWaitOnValidForwardQuery && a->second->numValidForwardQuery == a->second->numProcessedForwardQuery) {
    Debug("Unblocking commit for client id %lu seq num %lu", txn_client_id, txn_client_seq_num);
    a->second->commitWaitOnValidForwardQuery = false;
    a->second->ccb(COMMITTED);
  }
}

std::unique_ptr<TransactionMessage> ValidationClient::GetCompletedTxnMsg(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // GetCompletedTxn is called after validation has completed
    // so txn_id must be in allValTxnStates
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }
  std::unique_ptr<TransactionMessage> txn_msg = std::move(a->second->txn_msg);

  Debug(
    "ValidationClient::GetCompletedValTxn called for txn client id %lu, seq num %lu",
    txn_client_id,
    txn_client_seq_num
  );

  allValTxnStates.erase(a);
  return txn_msg;
}

} // namespace pelotonstore
