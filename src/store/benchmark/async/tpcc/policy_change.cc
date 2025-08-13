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

#include "store/benchmark/async/tpcc/policy_change.h"
#include "store/benchmark/async/tpcc/tpcc_utils.h"
#include "store/benchmark/async/tpcc/tpcc_common.h"
#include "store/benchmark/async/tpcc/tpcc-validation-proto.pb.h"
#include "store/common/common-proto.pb.h"
#include "store/common/policy/policy-proto.pb.h"

namespace tpcc {

PolicyChange::PolicyChange(uint32_t w_id) : w_id(w_id) {
  randWeight = std::uniform_int_distribution<uint32_t>(1, 3)(GetRand());
  std::cerr << "Changing policy p0 to weight " << randWeight << " for warehouse " << w_id << std::endl;
}

PolicyChange::PolicyChange(uint32_t w_id, uint32_t policy_change) : w_id(w_id), randWeight(policy_change) {
  std::cerr << "Changing policy p0 to weight " << randWeight << " for warehouse " << w_id << std::endl;
}

PolicyChange::~PolicyChange() {
}

void PolicyChange::SerializeTxnState(std::string &txnState) {
  TxnState currTxnState;
  std::string txn_name;
  txn_name.append(BENCHMARK_NAME);
  txn_name.push_back('_');
  txn_name.append(GetBenchmarkTxnTypeName(TXN_POLICY_CHANGE));
  currTxnState.set_txn_name(txn_name);

  validation::proto::PolicyChange curr_txn = validation::proto::PolicyChange();
  curr_txn.set_w_id(w_id);
  curr_txn.set_random_weight(randWeight);
  std::string txn_data;
  curr_txn.SerializeToString(&txn_data);
  currTxnState.set_txn_data(txn_data);

  currTxnState.SerializeToString(&txnState);
}

transaction_status_t PolicyChange::BaseExecute(SyncClient &client, int timeout, bool serialize) {
  Debug("POLICY_CHANGE");
  Debug("Warehouse: %u", w_id);

  std::string txnState;
  if(serialize) {
    PolicyChange::SerializeTxnState(txnState);
  }

  client.Begin(timeout, txnState);

  // distict table has policy id 1, change it to be policy of random weight
  PolicyObject policy;
  policy.set_policy_type(PolicyObject::WEIGHT_POLICY);
  WeightPolicyMessage weight_policy;
  weight_policy.set_weight(randWeight);
  weight_policy.SerializeToString(policy.mutable_policy_data());
  
  std::string policy_str;
  policy.SerializeToString(&policy_str);
  client.Put("p0", policy_str, timeout);

  return client.Commit(timeout);
}

}
