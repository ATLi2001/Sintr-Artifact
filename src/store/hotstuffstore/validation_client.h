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

#ifndef _HOTSTUFFSTORE_VALIDATION_CLIENT_H_
#define _HOTSTUFFSTORE_VALIDATION_CLIENT_H_

#include "lib/transport.h"
#include "store/common/timestamp.h"
#include "store/common/sintring/validation_client_common.h"
#include "store/common/sintring/params.h"
#include "store/hotstuffstore/common.h"
#include "store/hotstuffstore/hotstuff-sintr-proto.pb.h"
#include "store/hotstuffstore/pbft-proto.pb.h"
#include "store/hotstuffstore/server-proto.pb.h"

#include <string>
#include <vector>

#include "tbb/concurrent_hash_map.h"

namespace hotstuffstore {

typedef std::function<void(int, uint64_t, uint64_t, const std::string &,
  const std::string &, const Timestamp &)> validation_read_callback;
typedef std::function<void(int, const std::string &)> validation_read_timeout_callback;

class ValidationClient : public ::ValidationClientCommon {
 public:
  ValidationClient(Transport *transport, uint64_t client_id, SintrParameters sintr_params, Partitioner *part, int nShards, int nGroups);
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
  
  // Commit all Get(s) and Put(s) since Begin().
  virtual void Commit(commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout) override;

  // Abort all Get(s) and Put(s) since Begin().
  virtual void Abort(abort_callback acb, abort_timeout_callback atcb, uint32_t timeout) override;

  void ProcessForwardReadResult(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    proto::ForwardReadResult &&fwdReadResult);

  void NotifyForwardReadResultValid(uint64_t txn_client_id, uint64_t txn_client_seq_num);

  // return completed transaction for requested id
  std::unique_ptr<proto::Transaction> GetCompletedTxn(uint64_t txn_client_id, uint64_t txn_client_seq_num);
  void SetTxnTimestamp(uint64_t txn_client_id, uint64_t txn_client_seq_num, const Timestamp &ts);

 private:

   struct PendingValidationGet {
    PendingValidationGet(uint64_t txn_client_id, uint64_t txn_client_seq_num, const std::string &key) : 
        txn_client_id(txn_client_id), txn_client_seq_num(txn_client_seq_num), key(key) {}
    ~PendingValidationGet() {
      if (timeout != nullptr) {
        delete timeout;
      }
    }
    uint64_t txn_client_id;
    uint64_t txn_client_seq_num;
    std::string key;
    std::string value;
    validation_read_callback vrcb;
    Timestamp ts;
    validation_read_timeout_callback vrtcb;
    Timeout *timeout;
  };


  // for a (txn_client_id, txn_client_seq_num) pair, keep track of all relevant transaction state
  struct AllValidationTxnState {
    AllValidationTxnState() {}
    AllValidationTxnState(uint64_t txn_client_id, uint64_t txn_client_seq_num) :
      txn_client_id(txn_client_id), txn_client_seq_num(txn_client_seq_num), txn(std::make_unique<proto::Transaction>()) {}
    ~AllValidationTxnState() {
      // delete all pendingReadRequests
      for (auto &pendingGet : pendingGets) {
        delete pendingGet;
      }
      pendingGets.clear();
    }

    uint64_t txn_client_id;
    uint64_t txn_client_seq_num;
    // this tracks the readset/writeset of the transaction
    std::unique_ptr<proto::Transaction> txn;
    // prevent initiating client from hiding reads by telling validating client to ignore them
    // track all reads that should have been seen by the transaction
    std::set<std::string> seenReads;

    std::vector<PendingValidationGet *> pendingGets;

    // vector of (key, value) pairs
    std::vector<std::pair<std::string, std::pair<std::string, Timestamp>>> pendingForwardedRead;

    // for parallel read sig check case
    uint64_t numProcessedForwardRead = 0;
    uint64_t numValidForwardRead = 0;
    bool commitWaitOnValidForwardRead = false;
    commit_callback ccb;
    commit_timeout_callback ctcb;
  };

  bool IsParticipant(int g, const proto::Transaction &txn);

  // transport for timeout functionality
  Transport *transport;
  // My own client ID
  const uint64_t client_id;

  SintrParameters sintr_params;
  // Number of replica groups.
  uint64_t nshards;
  // Number of replica groups.
  uint64_t ngroups;
  Partitioner *part;

  // map from (txn_client_id, txn_client_seq_num) to all relevant validation txn state
  typedef tbb::concurrent_hash_map<std::string, AllValidationTxnState *> allValTxnStatesMap;
  allValTxnStatesMap allValTxnStates;
};

} // namespace hotstuffstore

#endif /* _HOTSTUFFSTORE_VALIDATION_CLIENT_H_ */
