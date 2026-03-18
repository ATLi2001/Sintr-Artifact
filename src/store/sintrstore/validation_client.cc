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

#include "store/sintrstore/validation_client.h"
#include "store/sintrstore/common.h"
#include "lib/message.h"

#include "store/common/query_result/query_result.h"
#include "store/common/query_result/query_result_proto_wrapper.h"
#include "store/common/query_result/query_result_proto_builder.h"

namespace sintrstore {

ValidationClient::ValidationClient(Transport *transport, uint64_t client_id, uint64_t nclients, uint64_t nshards, uint64_t ngroups, 
    Partitioner *part, std::string &table_registry, Parameters params, const PolicyCache *policyCache) : 
    transport(transport), client_id(client_id), nshards(nshards), ngroups(ngroups), part(part), params(params),
    table_registry(table_registry), policyCache(policyCache) {
      // init policy id function
      policyIdFunction = GetPolicyIdFunction(params.sintr_params.policyFunctionName);
    }

ValidationClient::~ValidationClient() {
  for (auto it = threadValtoSQL.begin(); it != threadValtoSQL.end(); ++it) {
    delete it->second;
  }
  threadValtoSQL.clear();

  for (auto it = allValTxnStates.begin(); it != allValTxnStates.end(); ++it) {
    delete it->second;
  }
  allValTxnStates.clear();
}

void ValidationClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {
  // if (query_to_commit_us.count > 0 && query_to_commit_us.count % 2000 == 0) {
  //   std::cerr << "Mean query to commit latency: " << query_to_commit_us.mean() << std::endl;
  // }
  // if (get_to_commit_us.count > 0 && get_to_commit_us.count % 2000 == 0) {
  //   std::cerr << "Mean get to commit latency: " << get_to_commit_us.mean() << std::endl;
  // }

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
  // define callback for when get completes
  validation_read_callback vrcb = [gcb, this](int status, uint64_t txn_client_id, uint64_t txn_client_seq_num, 
      const std::string &key, const std::string &value, const Timestamp &ts) {
    
    Debug("validation_read_callback on key %s, value %s", BytesToHex(key, 16).c_str(), BytesToHex(value, 16).c_str());
    gcb(status, key, value, ts);
  };

  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  Debug("Validation GET[%lu:%lu] key %s", txn_client_id, txn_client_seq_num, BytesToHex(key,16).c_str());

  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // Get should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }
  proto::Transaction *txn = a->second->txn;
  // edit the involved groups for txn
  std::vector<int> txnGroups(txn->involved_groups().begin(), txn->involved_groups().end());
  int i = (*part)(key, nshards, -1, txnGroups) % ngroups;
  if (!IsTxnParticipant(txn, i)) {
    txn->add_involved_groups(i);
  }

  a->second->seenReads.insert(key);

  // read locally in buffer
  // if (BufferGet(a->second, key, vrcb)) {
  //   Debug(
  //     "ValidationClient::BufferGet for client id %lu, seq num %lu, on key %s", 
  //     txn_client_id,
  //     txn_client_seq_num,
  //     BytesToHex(key, 16).c_str()
  //   );
  //   // remove pending Forwarded read from vector
  //   auto itr = std::find_if(
  //     a->second->pendingForwardedRead.begin(), a->second->pendingForwardedRead.end(),
  //     [&key](const auto &key_value) { return key_value.first == key; }
  //   );
  //   if(itr != a->second->pendingForwardedRead.end()) {
  //     Debug("removing pending forwarded read for key %s", BytesToHex(key, 16).c_str());
  //     a->second->pendingForwardedRead.erase(itr);
  //   }
  //   return;
  // }
  // check if forward read result already received (if callback exists)
  auto itr = std::find_if(
    a->second->pendingForwardedRead.begin(), a->second->pendingForwardedRead.end(),
    [&key](const auto &key_value) { return key_value.first == key; }
  );
  if(itr != a->second->pendingForwardedRead.end()) {
    std::pair<std::string, Timestamp> res = itr->second;
    vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, key, res.first, res.second);
    a->second->pendingForwardedRead.erase(itr);
    return;
  }

  Debug(
    "ValidationClient::Get registering PendingGet for client id %lu, seq num %lu on key %s", 
    txn_client_id, 
    txn_client_seq_num, 
    BytesToHex(key, 16).c_str()
  );

  // otherwise have to wait for read results to get passed over
  PendingValidationGet *pendingGet = new PendingValidationGet(txn_client_id, txn_client_seq_num);
  pendingGet->key = key;
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
      [curr_key=pendingGet->key](const PendingValidationGet *req) { return req->key == curr_key; }
    );
    if (reqs_itr == pendingGets.end()) {
      // pendingGet fulfilled
      return;
    }
    Panic("Timeout triggered for txn_id %s key %s", txn_id.c_str(), BytesToHex(pendingGet->key, 16).c_str());
    // pendingGet->vrtcb(REPLY_TIMEOUT, pendingGet->key);
  });

  pendingGet->timeout->Reset();
}

void ValidationClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb,
    uint32_t timeout) {
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  Debug("Validation PUT[%lu:%lu] key %s value %s", txn_client_id, txn_client_seq_num, BytesToHex(key,16).c_str(), BytesToHex(value,16).c_str());

  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // Put should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  proto::Transaction *txn = a->second->txn;
  WriteMessage *write = txn->add_write_set();
  write->set_key(key);
  write->set_value(value);
  a->second->write_values[key].push_back(value);
  if(params.sintr_params.liftingEnabled) {
    if(txn->policy_type() == proto::Transaction::POLICY_ID_POLICY) {
      // only write to policy keys
      a->second->policyIDs.insert(key);
    } else {
      a->second->policyIDs.insert(policyIdFunction(key, ""));
    }
  }

  if(txn->policy_type() == proto::Transaction::POLICY_ID_POLICY) {
    // add all shards as involved groups (since we are contacting all shards on a put to update policy)
    Debug("adding all involved shards in validation client");
    for(int i = 0; i < ngroups; i++) {
      if (!IsTxnParticipant(txn, i)) {
        txn->add_involved_groups(i);
      }
    }
  } else {
    std::vector<int> txnGroups(txn->involved_groups().begin(), txn->involved_groups().end());
    Debug("using non policy ID partitioner");
    int i = (*part)(key, nshards, -1, txnGroups) % ngroups;
    if (!IsTxnParticipant(txn, i)) {
      txn->add_involved_groups(i);
    }
  }

  a.release();
  pcb(REPLY_OK, key, value);
}

void ValidationClient::SQLRequest(std::string &statement, sql_callback scb,
  sql_timeout_callback stcb, uint32_t timeout){

  size_t pos;
  if((pos = statement.find(select_hook) != string::npos)){  
    Query(statement, std::move(scb), std::move(stcb), timeout);
  }
  else {
    Write(statement, std::move(scb), std::move(stcb), timeout);
  }
}

void ValidationClient::Write(std::string &write_statement, write_callback wcb,
  write_timeout_callback wtcb, uint32_t timeout, bool blind_write){ //blind_write: default false, must be explicit application choice to skip.

  Debug("Processing Write Statement: %s", write_statement.c_str());
  std::string read_statement;
  std::function<void(int, query_result::QueryResult*)>  write_continuation;
  bool skip_query_interpretation = false;
  uint64_t point_target_group = 0;

  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // Write should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }
  
  proto::Transaction *txn = a->second->txn;

  threadValtoSQLMap::accessor b;
  if (!threadValtoSQL.find(b, std::this_thread::get_id())) {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    Panic("cannot find thread ID %s in thread ID to SQL accessor", oss.str().c_str());
  }
  SQLTransformer *sql_interpreter = b->second;

  a->second->pendingWriteStatements.push_back(write_statement);

  std::function<void(void)> *delayed_blind_write_cb = nullptr;
  std::vector<std::string> *keys_written = new std::vector<std::string>();
  try{
    // tmp_ptr = nullptr means we don't need to wait
    auto tmp_ptr = &delayed_blind_write_cb;
    if (!params.sintr_params.blindWriteMessage) {
      tmp_ptr = nullptr;
    }
    else {
      // invariant - if there are pending blind writes, then blind write message count should be 0
      UW_ASSERT(a->second->pendingBlindWrites.size() == 0 || a->second->blind_write_message_count == 0);
      if (a->second->blind_write_message_count > 0) {
        tmp_ptr = nullptr;
      }
    }
    sql_interpreter->TransformWriteStatement(a->second->pendingWriteStatements.back(), read_statement, write_continuation, wcb, point_target_group, skip_query_interpretation, blind_write,
      keys_written, tmp_ptr);
  }
  catch(...){
    Panic("bug in transformer: %s -> %s", write_statement.c_str(), read_statement.c_str());
  }

  Debug("Transformed Write into re-con read_statement: %s", read_statement.c_str());

  //Testing/Debug only
  //  Debug("Current read set: Before next write.");
  //  for(auto read: txn.read_set()){
  //     Debug("Read set already contains: %s", read.key().c_str());
  //   }

  AllValidationTxnState *txn_ptr = a->second;

  auto write_cont = [this, write_continuation, keys_written, txn_ptr](int status, query_result::QueryResult *result){
    // Debug("validation write cont for client %lu with seq num %lu with write statement %s", txn_client_id, txn_client_seq_num, write_statement.c_str());
    write_continuation(status, result);
    // allValTxnStatesMap::accessor a;
    // Debug("After write cont");
    // if (!allValTxnStates.find(a, txn_id)) {
    //   // Write should always happen after SetTxnTimestamp, which inserts at txn_id
    //   Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
    // }

    // // update policy for current transaction
    if(params.sintr_params.liftingEnabled) {
      Debug("Before iterating through keys_written");
      if(keys_written == nullptr) {
        // no keys written?
        Warning("No keys written?");
        return;
      }
      for (const auto &key : *keys_written) {
        txn_ptr->policyIDs.insert(policyIdFunction(key, ""));
        // Debug("validation keys_written key %s for write statement %s from client %lu for seq num %lu", key.c_str(), write_statement.c_str(), txn_client_id, txn_client_seq_num);
      }
    }

    delete keys_written;
  };

  if(read_statement.empty()){ //Must be point operation (Insert/Delete)
    Debug("No read statement, immediately writing in validation client");  
    sql::QueryResultProtoWrapper *write_result = new sql::QueryResultProtoWrapper("");
    
    if (!IsTxnParticipant(txn, point_target_group)) {
      txn->add_involved_groups(point_target_group);
    }

    if (params.sintr_params.blindWriteMessage && a->second->blind_write_message_count > 0) {
      UW_ASSERT(delayed_blind_write_cb == nullptr);
      Debug("Blind write message already received, already edited txn");
      a->second->blind_write_message_count--;
    }

    if (delayed_blind_write_cb != nullptr) {
      UW_ASSERT(params.sintr_params.blindWriteMessage && a->second->blind_write_message_count == 0);

      Debug("Adding delayed blind write callback");
      a->second->pendingBlindWrites.push_back(delayed_blind_write_cb);
    }

    write_cont(REPLY_OK, write_result);
  }
  else{
    Debug("Issuing re-con Query validation");
    a.release();
    b.release();
    Query(read_statement, std::move(write_cont), wtcb, timeout, false, skip_query_interpretation); //cache_result = false
  }
  return;
}

void ValidationClient::Query(const std::string &query, query_callback qcb,
  query_timeout_callback qtcb, uint32_t timeout, bool cache_result, bool skip_query_interpretation) {

  UW_ASSERT(query.length() < ((uint64_t)1<<32));    
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);  
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    Panic("cannot find transaction %s in allValTxnStates for query", txn_id.c_str());
  }
  proto::Transaction *txn = a->second->txn;

  Debug("Query[%lu:%lu] (client:tx-seq). TS: [%lu:%lu]: %s.", 
      client_id, txn_client_seq_num, txn->timestamp().timestamp(), txn->timestamp().id(), query.c_str());
  
  PendingValidationQuery *pendingQuery;
  if(params.sintr_params.hideTimestamps) {
    pendingQuery = new PendingValidationQuery(Timestamp(), query, qcb, cache_result, txn->hashed_timestamp(), params.sintr_params.hashQueryGenId);
  } else {
    pendingQuery = new PendingValidationQuery(Timestamp(txn->timestamp()), query, qcb, cache_result, "", params.sintr_params.hashQueryGenId);
  } 

  threadValtoSQLMap::accessor b;
  if (!threadValtoSQL.find(b, std::this_thread::get_id())) {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    Panic("cannot find thread ID %s in thread ID to SQL accessor", oss.str().c_str());
  }
  SQLTransformer *sql_interpreter = b->second;

  // update involved groups for txn
  std::vector<int> txnGroups(txn->involved_groups().begin(), txn->involved_groups().end());
  int target_group = (*part)(pendingQuery->table_name, query, nshards, -1, txnGroups, false) % ngroups;
  std::vector<uint64_t> involved_groups = {target_group};
  for(auto &i: involved_groups){
    if (!IsTxnParticipant(txn, i)) {
      txn->add_involved_groups(i);
    }
  }
  
  pendingQuery->is_point = skip_query_interpretation? false : sql_interpreter->InterpretQueryRange(query, pendingQuery->table_name, pendingQuery->p_col_values, true); 
  Debug("Query is of type: %s ", pendingQuery->is_point? "POINT" : "RANGE");
  if(pendingQuery->is_point){
    Debug("Encoded key: %s", EncodeTableRow(pendingQuery->table_name, pendingQuery->p_col_values).c_str()); 
    std::string encoded_key = EncodeTableRow(pendingQuery->table_name, pendingQuery->p_col_values);

    a->second->seenReads.insert(encoded_key);
    auto read_itr = std::find_if(
      a->second->pendingForwardedPointQuery.begin(), a->second->pendingForwardedPointQuery.end(),
      [&encoded_key](const auto &keys) { return keys.first == encoded_key; }
    );
    if(read_itr != a->second->pendingForwardedPointQuery.end()) {
      Debug("Adding point query to readset for key %s", encoded_key.c_str());
      sql::QueryResultProtoWrapper* res = new sql::QueryResultProtoWrapper(read_itr->second);
      a->second->pendingForwardedPointQuery.erase(read_itr);
      qcb(REPLY_OK, res);
      delete pendingQuery;
      pendingQuery = nullptr;
      return;
    }

    // record the key
    pendingQuery->key = encoded_key;
  } 
  else{
    Debug("Query gen id: %s", BytesToHex(pendingQuery->query_gen_id, 16).c_str());

    a->second->seenQueries.insert(pendingQuery->query_gen_id);

    auto query_itr = std::find_if(
      a->second->pendingForwardedQuery.begin(), a->second->pendingForwardedQuery.end(),
      [&curr_query_gen_id = pendingQuery->query_gen_id](const auto &query_ids) { return query_ids.first == curr_query_gen_id; }
    );
    if(query_itr != a->second->pendingForwardedQuery.end()) {
      Debug("Adding query %s result to readset", BytesToHex(pendingQuery->query_gen_id, 16).c_str());
      sql::QueryResultProtoWrapper* res = new sql::QueryResultProtoWrapper(query_itr->second);
      a->second->pendingForwardedQuery.erase(query_itr);
      qcb(REPLY_OK, res);
      delete pendingQuery;
      pendingQuery = nullptr;
      return;
    }
  }

  Debug(
    "Registering PendingValidationQuery for client id %lu, seq num %lu on key %s", 
    txn_client_id, 
    txn_client_seq_num, 
    pendingQuery->key.c_str()
  );

  a->second->pendingQueries.push_back(pendingQuery);

  pendingQuery->timeout = new Timeout(transport, 2000, [this, txn_id, pendingQuery]() {
    allValTxnStatesMap::accessor a;
    if (!allValTxnStates.find(a, txn_id)) {
        // transaction has completed
      return;
    }
    std::vector<PendingValidationQuery *> pendingQueries = a->second->pendingQueries;
  
    auto reqs_itr = std::find_if(
      pendingQueries.begin(), pendingQueries.end(), 
      [curr_gen_id=pendingQuery->query_gen_id](const PendingValidationQuery *req) { return req->query_gen_id == curr_gen_id; }
    );
    if (reqs_itr == pendingQueries.end()) {
      // pendingQuery fulfilled
      return;
    }

    if (pendingQuery->is_point) {
      Panic("Timeout triggered for txn_id %s key %s", txn_id.c_str(), pendingQuery->key.c_str());
    }
    else {
      Panic("Timeout triggered for txn_id %s key %s", txn_id.c_str(), BytesToHex(pendingQuery->query_gen_id, 16).c_str());
    }
  });
  
  pendingQuery->timeout->Reset();

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
  // auto duration = end - pendingQuery->start_time;
  // pending_query_init_us.add(duration);
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

  bool pendingBlindWriteRemaining = a->second->pendingBlindWrites.size() > 0;
  while (params.sintr_params.blindWriteMessage && pendingBlindWriteRemaining) {
    // still need to wait for blind write message
    a.release();
    std::this_thread::yield();
    allValTxnStates.find(a, txn_id);
    pendingBlindWriteRemaining = a->second->pendingBlindWrites.size() > 0;
  }

  proto::Transaction *txn = a->second->txn;

  if (!a->second->pendingForwardedPointQuery.empty() || !a->second->pendingForwardedQuery.empty() ||
      !a->second->pendingForwardedRead.empty()) {
    // TODO: remove extra readset additions from transaction and send to initiating client
    // May also trigger if validating client receives duplicated messages due to asynchrony
    for(auto const &i : a->second->pendingForwardedQuery) {
      Debug("extra fwd query ID: %s", BytesToHex(i.first, 16).c_str());
    }
    for(auto const &i : a->second->pendingForwardedRead) {
      Debug("extra fwd read KEY: %s", BytesToHex(i.first, 16).c_str());
    }
    Panic("Transaction includes more values in readset than necessary, extra forwarded point queries: %d, forwarded queries: %d, forwarded reads: %d",
      a->second->pendingForwardedPointQuery.size(),
      a->second->pendingForwardedQuery.size(),
      a->second->pendingForwardedRead.size());
  }

  // prevent initiating client from hiding reads by telling validating client to ignore them
  std::set<std::string> readsInReadset;
  for (const auto &read : txn->read_set()) {
    readsInReadset.insert(read.key());
  }
  if (!params.query_params.cacheReadSet && params.query_params.mergeActiveAtClient) {
    // in this case, readsInReadset will potentially include query result reads in addition to seenReads
    if (!std::includes(readsInReadset.begin(), readsInReadset.end(),
        a->second->seenReads.begin(), a->second->seenReads.end())) {
      Panic("Transaction readset does not include all reads seen by the validating client.");
    }
  }
  else {
    if (readsInReadset != a->second->seenReads) {
      Panic("Transaction readset does not match the reads seen by the validating client.");
    }
  }
  if (a->second->queriesAddedToReadset != a->second->seenQueries) {
    Panic("Transaction queries added to readset does not match the queries seen by the validating client.");
  }

  // check cached values are in writeset/readset
  for (const auto& [key, ignore_vals] : a->second->ignore_readset_kv) {
    auto it = a->second->write_values.find(key);
    if (it == a->second->write_values.end()) {
      Panic("Key %s from ignore_readset_kv not found in write_values", BytesToHex(key, 16).c_str());
    }

    const auto& write_vals = it->second;
    size_t i = 0, j = 0;

    // Subsequence check: ignore_vals should appear in order in write_vals
    while (i < ignore_vals.size() && j < write_vals.size()) {
      if (ignore_vals[i] == write_vals[j]) {
          ++i;
        }
      ++j;
    }

    if (i != ignore_vals.size()) {
      Panic("Values for key %s in ignore_readset_kv are not a subsequence of write_values", BytesToHex(key, 16).c_str());
    }
  }
  
  Debug("Committing validation for client id %lu, seq num %lu and txn ID: %s", txn_client_id, txn_client_seq_num,
      BytesToHex(TransactionDigest(*txn, true), 16).c_str());
  // if has queries, and query deps are meant to be reported by client:
  // Sort and erase all duplicate dependencies. (equality = same txn_id and same involved group.)
  if(!txn->query_set().empty() && !params.query_params.cacheReadSet && params.query_params.mergeActiveAtClient){
    std::sort(txn->mutable_deps()->begin(), txn->mutable_deps()->end(), sortDepSet);
    // erases all but last appearance
    txn->mutable_deps()->erase(std::unique(txn->mutable_deps()->begin(), txn->mutable_deps()->end(), equalDep), txn->mutable_deps()->end());
  }

  for(auto &[table_name, table_write] : txn->table_writes()){
    if(table_write.has_changed_table() && table_write.changed_table()){
      WriteMessage *table_ver = txn->add_write_set();
      table_ver->set_key(EncodeTable(table_name));
      table_ver->set_value("");
      table_ver->set_is_table_col_version(true);
      table_ver->mutable_rowupdates()->set_row_idx(-1); 
    }
  }

  // add policies to validation transaction if lift is true
  if(params.sintr_params.liftingEnabled && txn->lift_keys_size() > 0) {
    for (const auto& policyId : a->second->policyIDs) {
      proto::PolicyVersion policyVersion = proto::PolicyVersion();
      Debug("Setting policy ID %s for validation", policyId.c_str());
      policyVersion.set_policy_id(policyId);
      Timestamp policyTimestamp = policyCache->GetTimestamp(policyId);
      policyVersion.mutable_timestamp()->set_id(policyTimestamp.getID());
      policyVersion.mutable_timestamp()->set_timestamp(policyTimestamp.getTimestamp());
      *txn->add_policy_versions() = policyVersion;
    }
  }

  if (params.sintr_params.parallelQuerySigsCheck && ((a->second->numValidForwardQuery < a->second->numProcessedForwardQuery) || (a->second->numValidReads < a->second->numPendingReads))) {
    // still need to wait for all forward queries to be validated
    // call commit callback later
    Debug(
      "Validating for client id %lu seq num %lu commit received %lu of %lu parallel query sigs check",
      txn_client_id,
      txn_client_seq_num,
      a->second->numValidForwardQuery,
      a->second->numProcessedForwardQuery
    );
    Debug(
      "Validating for client id %lu seq num %lu commit received %lu of %lu parallel query sigs check",
      txn_client_id,
      txn_client_seq_num,
      a->second->numValidReads,
      a->second->numPendingReads
    );
    a->second->commitWaitOnValidForwardQuery = true;
    a->second->ccb = std::move(ccb);
    a->second->ctcb = std::move(ctcb);
    return;
  }

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
  // auto duration = end - query_fin_us;
  // query_to_commit_us.add(duration);
  // auto duration = end - get_fin_us;
  // get_to_commit_us.add(duration);

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
  if(!params.sintr_params.parallelQuerySigsCheck || (a->second->numPendingReads == a->second->numValidReads
      && a->second->numProcessedForwardQuery == a->second->numValidForwardQuery)) {
    delete a->second->txn;
    delete a->second;
    allValTxnStates.erase(a);
  } else {
    a->second->aborted = true;
  }
  a.release();
  acb();
}

const PolicyCache& ValidationClient::GetPolicyCache() const {
  return *policyCache;
}

void ValidationClient::LiftTransaction(std::vector<std::string> &lift_keys) {
  if(!params.sintr_params.liftingEnabled || lift_keys.size() == 0) {
    return;
  }
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // LiftTransaction should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  Debug("Lifting Validation Transaction[%lu:%lu]", txn_client_id, txn_client_seq_num);
  // auto* txn_lift_keys = a->second->txn->mutable_lift_keys();
  a->second->txn->mutable_lift_keys()->Reserve(lift_keys.size());
  for (auto& s : lift_keys) {
    *a->second->txn->add_lift_keys() = std::move(s);
  }
}

void ValidationClient::SetThreadValSQLInterpreter() {
  threadValtoSQLMap::accessor a;
  const bool isNewKey = threadValtoSQL.insert(a, std::this_thread::get_id());
  if (isNewKey) {
    Debug("Setting new sql transformer");
    SQLTransformer *sql_transformer = new SQLTransformer(&params.query_params);
    sql_transformer->RegisterTables(table_registry);
    sql_transformer->RegisterPartitioner(part, nshards, ngroups, -1);
    a->second = sql_transformer;
  }
}

void ValidationClient::SetTxnTimestamp(uint64_t txn_client_id, uint64_t txn_client_seq_num, const Timestamp &ts, bool isPolicyTransaction,
    const std::string &hashed_ts) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  const bool isNewKey = allValTxnStates.insert(a, txn_id);
  proto::Transaction *txn;
  if (isNewKey) {
    txn = new proto::Transaction();
    txn->set_client_id(txn_client_id);
    txn->set_client_seq_num(txn_client_seq_num);
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num, txn);
  } 
  else {
    txn = a->second->txn;
  }
  if(params.sintr_params.hideTimestamps) {
    txn->set_hashed_timestamp(hashed_ts);
  } else {
    ts.serialize(txn->mutable_timestamp()); // sets to default ts if hide timestamps is true
  }
  if(isPolicyTransaction) {
    txn->set_policy_type(proto::Transaction::POLICY_ID_POLICY);
  }
  
  if(params.query_params.sql_mode && txn->policy_type() != proto::Transaction::POLICY_ID_POLICY) {
    threadValtoSQLMap::accessor b;
    if (!threadValtoSQL.find(b, std::this_thread::get_id())) {
      std::ostringstream oss;
      oss << std::this_thread::get_id();
      Panic("cannot find thread ID %s in thread ID to SQL accessor", oss.str().c_str());
    }
    Debug("CREATING NEW TX for client id %lu, seq num %lu", txn_client_id, txn_client_seq_num);
    b->second->NewTx(txn);
  }
}

void ValidationClient::ProcessForwardReadResult(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    const proto::ForwardReadResult &fwdReadResult, const proto::Dependency &dep, bool hasDep, bool addReadset) {
  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();
  Timestamp curr_ts;
  std::string hashed_ts = "";
  if(!params.sintr_params.hideTimestamps) {
    curr_ts = Timestamp(fwdReadResult.timestamp());
  } else {
    hashed_ts = fwdReadResult.hashed_timestamp();
  }
  Debug(
    "ProcessForwardReadResult from client id %lu, seq num %lu for key %s", 
    txn_client_id,
    txn_client_seq_num,
    BytesToHex(curr_key, 16).c_str()
  );

  // lambda for editing txn state
  auto editTxnStateCB = [
    this, &curr_key, &curr_value, &curr_ts, &dep, hasDep, addReadset, &hashed_ts
  ](AllValidationTxnState *allValTxnState) {
    if (addReadset) {
      Debug("ADDING TO NUMPENDING READS HERE %s FOR client seq num %lu", curr_key.c_str(), allValTxnState->txn_client_seq_num);
      ++allValTxnState->numPendingReads;
      AddReadset(allValTxnState, curr_key, curr_value, curr_ts, true, false, hashed_ts);
      std::string policyId = IsPolicyId(curr_key) ? curr_key : policyIdFunction(curr_key, "");
      if(params.sintr_params.liftingEnabled && policyCache->GetTimestamp(policyId) <= curr_ts) {
        allValTxnState->policyIDs.insert(policyId);
      }
    } else {
      Debug("ADDING TO CACHED READS HERE %s FOR client seq num %lu", curr_key.c_str(), allValTxnState->txn_client_seq_num);
      auto it = allValTxnState->readValues.find(curr_key);
      if (it == allValTxnState->readValues.end()) {
          // Key doesn't exist
          Panic("Cached read result or key %s not added to txn", BytesToHex(curr_key, 16).c_str());
      }
      if(it->second != curr_value) {
        // value doesn't exist, add to the ignore readset kv list
        allValTxnState->ignore_readset_kv[curr_key].push_back(curr_value);
      }
    }
    if (hasDep) {
      AddDep(allValTxnState, dep);
    }
  };

  // find matching pending get by first going off txn client id and sequence number, then key
  // if forwarded read result is for a get that the validation transaction has not yet gotten to,
  // add it to the appropriate transaction readset

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
    proto::Transaction *txn = new proto::Transaction();
    txn->set_client_id(txn_client_id);
    txn->set_client_seq_num(txn_client_seq_num);
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num, txn);
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

  struct timespec ts_end;
  clock_gettime(CLOCK_MONOTONIC, &ts_end);
  uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
  auto duration = end - req->start_time;
  // Warning("PendingValidationGet took %lu us", duration);
  // pending_get_us.add(duration);
  // get_fin_us = end;

  req->ts = curr_ts;
  editTxnStateCB(a->second);
  req->vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, req->key, curr_value, req->ts);

  // remove from vector
  reqs->erase(reqs_itr);
  // free memory
  delete req;
}

void ValidationClient::ProcessForwardPointQueryResult(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    const proto::ForwardReadResult &fwdReadResult, const proto::Dependency &dep, bool hasDep, bool addReadset) {
  // struct timespec ts_start;
  // clock_gettime(CLOCK_MONOTONIC, &ts_start);
  // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();
  std::string hashed_ts = "";
  Timestamp curr_ts;
  if(!params.sintr_params.hideTimestamps) {
    curr_ts = Timestamp(fwdReadResult.timestamp());
  } else {
    hashed_ts = fwdReadResult.hashed_timestamp();
    // Debug("HASHED TS FOR VALIDATION CLIENT forward point query: %s", BytesToHex(hashed_ts, 16).c_str());
  }
  Debug(
    "ProcessForwardPointQueryResult from client id %lu, seq num %lu for key %s", 
    txn_client_id,
    txn_client_seq_num,
    curr_key.c_str()
  );

  // lambda for editing txn state
  auto editTxnStateCB = [
    this, &curr_key, &curr_value, &curr_ts, &dep, hasDep, addReadset, &hashed_ts
  ](AllValidationTxnState *allValTxnState, const std::string &query_cmd) {
    if (addReadset) {
      // bool cache_point = !curr_value.empty() && query_cmd.find("SELECT *") != std::string::npos;
      ++allValTxnState->numPendingReads;
      AddReadset(allValTxnState, curr_key, curr_value, curr_ts, false, false, hashed_ts);
      std::string policyId = policyIdFunction(curr_key, "");
      if(params.sintr_params.liftingEnabled && allValTxnState->policyIDs.find(policyId) == allValTxnState->policyIDs.end()
         && policyCache->GetTimestamp(policyId) <= curr_ts) {
        allValTxnState->policyIDs.insert(policyId);
      }
    } else {
      // check keys already added to txn
      auto it = allValTxnState->readValues.find(curr_key);
      if (it == allValTxnState->readValues.end() || it->second != curr_value) {
          // Key doesn't exist OR value doesn't match
          Panic("Cached point read result %s or key %s not added to txn", BytesToHex(curr_value, 16).c_str(), BytesToHex(curr_key, 16).c_str());
      }
    }
    if (hasDep) {
      AddDep(allValTxnState, dep);
    }
  };

  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  const bool isNewKey = allValTxnStates.insert(a, curr_txn_id);
  if (isNewKey) {
    Debug(
      "ProcessForwardPointQueryResult from client id %lu, seq num %lu, before txn_id in allValTxnStates registered for key %s", 
      txn_client_id,
      txn_client_seq_num,
      curr_key.c_str()
    );
    proto::Transaction *txn = new proto::Transaction();
    txn->set_client_id(txn_client_id);
    txn->set_client_seq_num(txn_client_seq_num);
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num, txn);
    editTxnStateCB(a->second, ""); // use empty string for query, maybe cache result when query is executed
    a->second->pendingForwardedPointQuery.push_back(std::make_pair(curr_key, curr_value));
    return;
  }

  std::vector<PendingValidationQuery *> *reqs = &a->second->pendingQueries;
  auto reqs_itr = std::find_if(
    reqs->begin(), reqs->end(),
    [&curr_key](const PendingValidationQuery *req) { return req->is_point && req->key == curr_key; }
  );
  if (reqs_itr == reqs->end()) {
    Debug(
      "ProcessForwardPointQueryResult from client id %lu, seq num %lu, before PendingValidationQuery registered for key %s", 
      txn_client_id,
      txn_client_seq_num,
      curr_key.c_str()
    );
    editTxnStateCB(a->second, ""); // use empty string for query, maybe cache result when query is executed
    a->second->pendingForwardedPointQuery.push_back(std::make_pair(curr_key, curr_value));
    return;
  }
  // callback
  PendingValidationQuery *req = *reqs_itr;

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // query_fin_us = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
  
  req->ts = curr_ts;
  editTxnStateCB(a->second, req->query_cmd);
  sql::QueryResultProtoWrapper *q_result = new sql::QueryResultProtoWrapper(curr_value);
  req->vqcb(REPLY_OK, q_result);

  // remove from vector
  reqs->erase(reqs_itr);
  // free memory
  delete req;
}

void ValidationClient::ProcessForwardQueryResult(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    const proto::ForwardQueryResult &fwdQueryResult, bool addReadset) {
  std::string curr_query_gen_id = fwdQueryResult.query_gen_id();
  std::string curr_query_result = fwdQueryResult.query_result();
  Debug(
    "ProcessForwardQueryResult from client id %lu, seq num %lu, query gen id %s, query result %s", 
    txn_client_id, 
    txn_client_seq_num,
    BytesToHex(curr_query_gen_id, 16).c_str(),
    BytesToHex(curr_query_result, 16).c_str()
  );

  // lambda for editing txn state
  auto editTxnStateCB = [
    this, &curr_query_gen_id, &curr_query_result, &fwdQueryResult, addReadset
  ](AllValidationTxnState *allValTxnState, const std::string &query_cmd, sql::QueryResultProtoWrapper *q_result, bool cache_result) {
    if (addReadset) {
      ++allValTxnState->numProcessedForwardQuery;
      allValTxnState->queriesAddedToReadset.insert(curr_query_gen_id);
      allValTxnState->added_query_results.insert(curr_query_result);
      AddQueryReadset(allValTxnState, fwdQueryResult);
    }
    else {
      // this is a cached message sent over by client...
      // check if it's already been added to txn
      if(allValTxnState->queriesAddedToReadset.find(curr_query_gen_id) == allValTxnState->queriesAddedToReadset.end()
        || allValTxnState->added_query_results.find(curr_query_result) == allValTxnState->added_query_results.end()) {
        Panic("cached query %s or query result %s is not in transaction", query_cmd.c_str(), curr_query_result.c_str());
      }
    }
  };

  // find matching pending query by first going off txn client id and sequence number, then query_gen_id
  // if forwarded query result is for a query that the validation transaction has not yet gotten to,
  // add it to the appropriate transaction query result cache

  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  const bool isNewKey = allValTxnStates.insert(a, curr_txn_id);
  if (isNewKey) {
    Debug(
      "ProcessForwardQueryResult from client id %lu, seq num %lu, before txn_id in allValTxnStates registered for query %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(curr_query_gen_id, 16).c_str()
    );
    proto::Transaction *txn = new proto::Transaction();
    txn->set_client_id(txn_client_id);
    txn->set_client_seq_num(txn_client_seq_num);
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num, txn);
    editTxnStateCB(a->second, "", nullptr, false);
    a->second->pendingForwardedQuery.push_back(std::make_pair(curr_query_gen_id, curr_query_result));
    return;
  }

  std::vector<PendingValidationQuery *> *reqs = &a->second->pendingQueries;
  auto reqs_itr = std::find_if(
    reqs->begin(), reqs->end(), 
    [&curr_query_gen_id](const PendingValidationQuery *req) { return req->query_gen_id == curr_query_gen_id; }
  );
  if (reqs_itr == reqs->end()) {
    Debug(
      "ProcessForwardQueryResult from client id %lu, seq num %lu, before PendingQuery registered for query %s", 
      txn_client_id,
      txn_client_seq_num,
      BytesToHex(curr_query_gen_id, 16).c_str()
    );
    editTxnStateCB(a->second, "", nullptr, false);
    a->second->pendingForwardedQuery.push_back(std::make_pair(curr_query_gen_id, curr_query_result));
    return;
  }

  // callback
  PendingValidationQuery *req = *reqs_itr;

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // query_fin_us = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;

  sql::QueryResultProtoWrapper *q_result = new sql::QueryResultProtoWrapper(curr_query_result);
  editTxnStateCB(a->second, req->query_cmd, q_result, req->cache_result);
  req->vqcb(REPLY_OK, q_result);
  // no need to delete q_result since the query callback will take care of it

  // remove from vector
  reqs->erase(reqs_itr);
  // free memory
  delete req;
}

void ValidationClient::NotifyForwardQueryResultValid(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  const bool isNewKey = allValTxnStates.insert(a, curr_txn_id);
  if (isNewKey) {
    // this could happen if validation of signature somehow finishes before txn is added
    Debug("First new transaction %s in allValTxnStates", curr_txn_id.c_str());
    proto::Transaction *txn = new proto::Transaction();
    txn->set_client_id(txn_client_id);
    txn->set_client_seq_num(txn_client_seq_num);
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num, txn);
  }
  ++a->second->numValidForwardQuery;
  Debug(
    "numValidForwardQuery for client id %lu seq num %lu is now %lu",
    txn_client_id, txn_client_seq_num, a->second->numValidForwardQuery
  );
  if(a->second->aborted && a->second->numProcessedForwardQuery == a->second->numValidForwardQuery && a->second->numPendingReads == a->second->numValidReads) {
    // clean up aborted txn for query
    delete a->second->txn;
    delete a->second;
    allValTxnStates.erase(a);
    return;
  }
  if (a->second->commitWaitOnValidForwardQuery && a->second->numValidForwardQuery == a->second->numProcessedForwardQuery && a->second->numPendingReads == a->second->numValidReads) {
    Debug("Unblocking commit for client id %lu seq num %lu", txn_client_id, txn_client_seq_num);
    a->second->commitWaitOnValidForwardQuery = false;
    a->second->ccb(COMMITTED);
  }
}

void ValidationClient::NotifyForwardReadResultValid(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (allValTxnStates.insert(a, curr_txn_id)) {
    // this could happen if validation of signature somehow finishes before txn is added
    //TODO: Figure out if I have to make this section atomic
    Debug("First new transaction %s in allValTxnStates", curr_txn_id.c_str());
    proto::Transaction *txn = new proto::Transaction();
    txn->set_client_id(txn_client_id);
    txn->set_client_seq_num(txn_client_seq_num);
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num, txn);
  }
  ++a->second->numValidReads;
  Debug(
    "numValidReads for client id %lu seq num %lu is now %lu while numPendingReads is %lu",
    txn_client_id, txn_client_seq_num, a->second->numValidReads, a->second->numPendingReads
  );
  if(a->second->aborted && a->second->numPendingReads == a->second->numValidReads && a->second->numProcessedForwardQuery == a->second->numValidForwardQuery) {
    // clean up aborted txn
    delete a->second->txn;
    delete a->second;
    allValTxnStates.erase(a);
    return;
  }
  if (a->second->commitWaitOnValidForwardQuery && a->second->numPendingReads == a->second->numValidReads && a->second->numProcessedForwardQuery == a->second->numValidForwardQuery) {
    Debug("Unblocking commit for client id %lu seq num %lu", txn_client_id, txn_client_seq_num);
    a->second->commitWaitOnValidForwardQuery = false;
    a->second->ccb(COMMITTED);
  }
}


void ValidationClient::ProcessBlindWrite(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string curr_txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  
  allValTxnStatesMap::accessor a;
  const bool isNewKey = allValTxnStates.insert(a, curr_txn_id);
  if (isNewKey) {
    Debug(
      "ProcessBlindWrite from client id %lu, seq num %lu, before txn_id in allValTxnStates registered",
      txn_client_id,
      txn_client_seq_num
    );
    proto::Transaction *txn = new proto::Transaction();
    txn->set_client_id(txn_client_id);
    txn->set_client_seq_num(txn_client_seq_num);
    a->second = new AllValidationTxnState(txn_client_id, txn_client_seq_num, txn);
    a->second->blind_write_message_count++;
    return;
  }

  if (a->second->pendingBlindWrites.size() == 0) {
    Debug(
      "ProcessBlindWrite from client id %lu, seq num %lu, before PendingBlindWrite registered",
      txn_client_id,
      txn_client_seq_num
    );
    a->second->blind_write_message_count++;
    return;
  }

  Debug(
    "ProcessBlindWrite from client id %lu, seq num %lu, executing pending blind write",
    txn_client_id,
    txn_client_seq_num
  );

  // there exists a pending blind write that can now be executed
  std::function<void(void)> *delayed_blind_write_cb = a->second->pendingBlindWrites.front();
  (*delayed_blind_write_cb)();
  a->second->pendingBlindWrites.pop_front();
  delete delayed_blind_write_cb;
}

proto::Transaction *ValidationClient::GetCompletedTxn(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);
  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // GetCompletedTxn is called after validation has completed
    // so txn_id must be in allValTxnStates
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }
  proto::Transaction *txn = a->second->txn;
  UW_ASSERT(txn->client_id() == txn_client_id);
  UW_ASSERT(txn->client_seq_num() == txn_client_seq_num);

  Debug(
    "ValidationClient::GetCompletedValTxn called for txn client id %lu, seq num %lu",
    txn_client_id,
    txn_client_seq_num
  );
  // does not remove txn;
  delete a->second;

  allValTxnStates.erase(a);
  return txn;
}

// DEPRECATED
bool ValidationClient::BufferGet(const AllValidationTxnState *allValTxnState, const std::string &key, 
    validation_read_callback vrcb) {
  uint64_t txn_client_id = allValTxnState->txn_client_id;
  uint64_t txn_client_seq_num = allValTxnState->txn_client_seq_num;
  proto::Transaction *txn = allValTxnState->txn;
  for (const auto &write : txn->write_set()) {
    if (write.key() == key) {
      vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, key, write.value(), Timestamp());
      return true;
    }
  }

  for (const auto &read : txn->read_set()) {
    if (read.key() == key) {
      vrcb(REPLY_OK, txn_client_id, txn_client_seq_num, key, allValTxnState->readValues.at(key), read.readtime());
      return true;
    }
  }

  return false;
}

void ValidationClient::AddReadset(AllValidationTxnState *allValTxnState,
    const std::string &key, const std::string &value, const Timestamp &ts,
    bool is_get, bool cache_point, std::string hashedTS) {
  // add to readset
  proto::Transaction *txn = allValTxnState->txn;
  ReadMessage *read = txn->add_read_set();
  read->set_key(key);
  if(!params.sintr_params.hideTimestamps) {
    ts.serialize(read->mutable_readtime());
  } else {
    read->set_hashed_readtime(hashedTS);
  }
  // this is just to track all keys added to the readset (regardless if it's a point query or get)
  allValTxnState->readValues[key] = value;
}

void ValidationClient::AddQueryReadset(AllValidationTxnState *allValTxnState,
    const proto::ForwardQueryResult &fwdQueryResult) {

  proto::Transaction *txn = allValTxnState->txn;

  proto::QueryResultMetaData *queryRep = txn->add_query_set();
  queryRep->set_query_id(fwdQueryResult.query_res_meta().query_id());
  queryRep->set_retry_version(fwdQueryResult.query_res_meta().retry_version());

  for (const auto &[group, queryMeta] : fwdQueryResult.query_res_meta().group_meta()) {
    if (params.query_params.cacheReadSet) {
      proto::QueryGroupMeta &queryMD = (*queryRep->mutable_group_meta())[group]; 
      queryMD.set_read_set_hash(queryMeta.read_set_hash());
    }
    else {
      if (params.query_params.mergeActiveAtClient) {
        for (const auto &read : queryMeta.query_read_set().read_set()) {
          // if(params.sintr_params.hideTimestamps) {
          //   UW_ASSERT(!read.has_readtime());
          // }
          *txn->add_read_set() = read;
          // insert read policy ID
          std::string policyId = policyIdFunction(read.key(), "");
          if(params.sintr_params.liftingEnabled && allValTxnState->policyIDs.find(policyId) == allValTxnState->policyIDs.end()
            && policyCache->GetTimestamp(policyId) <= read.readtime()) {
            allValTxnState->policyIDs.insert(policyId);
          }
          allValTxnState->readValues[read.key()] = fwdQueryResult.query_result(); 
          // value isn't given in query MD
          // just return query result string, can parse in tpcc_client?
        }
        for (const auto &dep : queryMeta.query_read_set().deps()){
          *txn->add_deps() = dep;
        }
        for (const auto &pred: queryMeta.query_read_set().read_predicates()){
          if(!txn->read_predicates().empty() && pred.pred_instances_size() == 1){ //This is just a simple check that sees if there are 2 consecutive preds (that only have 1 instantiation) with the same pred_instance
            if(pred.pred_instances()[0] == txn->read_predicates()[txn->read_predicates_size()-1].pred_instances()[0]) continue;
          }
          *txn->add_read_predicates() = pred;
        }
      }
      else {
        proto::QueryGroupMeta &queryMD = (*queryRep->mutable_group_meta())[group];
        *queryMD.mutable_query_read_set() = queryMeta.query_read_set();
      }
    }
  }
}

void ValidationClient::AddDep(AllValidationTxnState *allValTxnState, const proto::Dependency &dep) {
  proto::Transaction *txn = allValTxnState->txn;
  *txn->add_deps() = dep;
}

bool ValidationClient::IsTxnParticipant(proto::Transaction *txn, int g) {
  for (const auto &participant : txn->involved_groups()) {
    if (participant == g) {
      return true;
    }
  }
  return false;
}

const std::map<std::string, std::string> &ValidationClient::GetReadset() {
  uint64_t txn_client_id, txn_client_seq_num;
  GetThreadValTxnId(txn_client_id, txn_client_seq_num);
  std::string txn_id = ToTxnId(txn_client_id, txn_client_seq_num);

  allValTxnStatesMap::accessor a;
  if (!allValTxnStates.find(a, txn_id)) {
    // LiftTransaction should always happen after SetTxnTimestamp, which inserts at txn_id
    Panic("cannot find transaction %s in allValTxnStates", txn_id.c_str());
  }

  Debug("Returning readset of txn[%lu:%lu]", txn_client_id, txn_client_seq_num);
  return a->second->readValues;
}


} // namespace sintrstore
