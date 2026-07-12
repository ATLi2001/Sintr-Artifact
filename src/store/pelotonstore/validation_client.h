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

#ifndef _PELOTONSTORE_VALIDATION_CLIENT_H_
#define _PELOTONSTORE_VALIDATION_CLIENT_H_

#include "lib/transport.h"
#include "store/common/sintring/validation_client_common.h"
#include "store/common/sintring/params.h"
#include "store/pelotonstore/common.h"
#include "store/pelotonstore/peloton-sintr-proto.pb.h"

#include <string>
#include <vector>

#include "tbb/concurrent_hash_map.h"

namespace pelotonstore {

class ValidationClient : public ::ValidationClientCommon {
 public:
  ValidationClient(Transport *transport, uint64_t client_id, SintrParameters sintr_params);
  virtual ~ValidationClient();

  // Begin a transaction.
  virtual void Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry = false, const std::string &txnState = std::string()) override;

  // Get the value corresponding to key.
  virtual void Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) override;

  // Set the value for the given key.
  virtual void Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb, uint32_t timeout) override;

  virtual void SQLRequest(std::string &statement, sql_callback scb,
    sql_timeout_callback stcb, uint32_t timeout) override;
  
  virtual void Write(std::string &write_statement, write_callback wcb,
      write_timeout_callback wtcb, uint32_t timeout, bool blind_write = false) override;
  
  virtual void Query(const std::string &query, query_callback qcb,
    query_timeout_callback qtcb, uint32_t timeout, bool cache_result = false, bool skip_query_interpretation = false) override;
  
  // Commit all Get(s) and Put(s) since Begin().
  virtual void Commit(commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout) override;

  // Abort all Get(s) and Put(s) since Begin().
  virtual void Abort(abort_callback acb, abort_timeout_callback atcb, uint32_t timeout) override;

  void ProcessForwardSQLResult(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    proto::ForwardSQLResult &&fwdSQLResult);

  // for the parallel query sig check case, should be later notified of validity
  void NotifyForwardQueryResultValid(uint64_t txn_client_id, uint64_t txn_client_seq_num);

  // return completed transaction for requested id
  std::unique_ptr<TransactionMessage> GetCompletedTxnMsg(uint64_t txn_client_id, uint64_t txn_client_seq_num);
  void SetTxnTimestamp(uint64_t txn_client_id, uint64_t txn_client_seq_num);

 private:
  struct PendingValidationSQLRequest {
    PendingValidationSQLRequest(const std::string &statement, sql_callback scb, sql_timeout_callback stcb,
        uint64_t txn_client_id, uint64_t txn_client_seq_num, bool hashQueryGenId) :
        statement(statement), vscb(scb), vstcb(stcb), timeout(nullptr) {
      sql_gen_id = SQLGenId(statement, txn_client_id, txn_client_seq_num, hashQueryGenId);
    }
    ~PendingValidationSQLRequest() {
      if (timeout != nullptr) {
        delete timeout;
      }
    }

    std::string statement;
    std::string sql_gen_id;

    sql_callback vscb;
    sql_timeout_callback vstcb;
    Timeout *timeout;
  };


  // for a (txn_client_id, txn_client_seq_num) pair, keep track of all relevant transaction state
  struct AllValidationTxnState {
    AllValidationTxnState() {}
    AllValidationTxnState(uint64_t txn_client_id, uint64_t txn_client_seq_num) :
      txn_client_id(txn_client_id), txn_client_seq_num(txn_client_seq_num), txn_msg(std::make_unique<TransactionMessage>()) {}
    ~AllValidationTxnState() {
      // delete all pendingSQLRequests
      for (auto &pendingSQLRequest : pendingSQLRequests) {
        delete pendingSQLRequest;
      }
      pendingSQLRequests.clear();
    }

    uint64_t txn_client_id;
    uint64_t txn_client_seq_num;
    // this tracks the readset/writeset of the transaction
    std::unique_ptr<TransactionMessage> txn_msg;

    std::vector<PendingValidationSQLRequest *> pendingSQLRequests;

    // vector of (sql gen ID, sql result) pairs
    std::vector<std::pair<std::string, std::string>> pendingForwardedSQLResults;

    // for parallel query sig check case
    uint64_t numProcessedForwardQuery = 0;
    uint64_t numValidForwardQuery = 0;
    bool commitWaitOnValidForwardQuery = false;
    commit_callback ccb;
    commit_timeout_callback ctcb;
  };

  // transport for timeout functionality
  Transport *transport;
  // My own client ID
  const uint64_t client_id;

  SintrParameters sintr_params;

  // map from (txn_client_id, txn_client_seq_num) to all relevant validation txn state
  typedef tbb::concurrent_hash_map<std::string, AllValidationTxnState *> allValTxnStatesMap;
  allValTxnStatesMap allValTxnStates;
};

} // namespace pelotonstore

#endif /* _PELOTONSTORE_VALIDATION_CLIENT_H_ */
