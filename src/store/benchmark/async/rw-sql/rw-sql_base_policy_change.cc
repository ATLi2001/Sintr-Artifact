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

#include "store/benchmark/async/rw-sql/rw-sql_base_policy_change.h"
#include "store/benchmark/async/rw-sql/rw-sql_common.h"
#include "store/common/common-proto.pb.h"
#include "store/common/policy/policy-proto.pb.h"
#include "store/common/policy/policy_id.h"
#include "store/benchmark/async/rw-sql/rw-sql-validation-proto.pb.h"

#include <fmt/core.h>


namespace rwsql {

RWSQLBasePolicyChange::RWSQLBasePolicyChange(uint64_t policy_id, uint32_t policy_weight)
  : policy_id(policy_id), policy_weight(policy_weight) {}

RWSQLBasePolicyChange::~RWSQLBasePolicyChange() {}

transaction_status_t RWSQLBasePolicyChange::BaseExecute(SyncClient &client, uint32_t timeout, bool serialize) {
  Debug("POLICY_CHANGE");
  Debug("policy_id: %lu", policy_id);

  std::string txnState;
  if (serialize) {
    SerializeTxnState(txnState);
  }
  client.Begin(timeout, txnState);

  PolicyObject policy;
  policy.set_policy_type(PolicyObject::WEIGHT_POLICY);
  WeightPolicyMessage weight_policy;
  weight_policy.set_weight(policy_weight);
  weight_policy.SerializeToString(policy.mutable_policy_data());
  
  std::string policy_str;
  policy.SerializeToString(&policy_str);

  client.Put(PolicyIdString(policy_id), policy_str, timeout);

  return client.Commit(timeout);
}

void RWSQLBasePolicyChange::SerializeTxnState(std::string &txnState) {
  TxnState currTxnState;
  std::string txn_name;
  txn_name.append(BENCHMARK_NAME);
  txn_name.push_back('_');
  txn_name.append(GetBenchmarkTxnTypeName(RW_SQL_POLICY_CHANGE));
  currTxnState.set_txn_name(txn_name);

  validation::proto::RWSqlPolicyChange curr_txn = validation::proto::RWSqlPolicyChange();
  curr_txn.set_policy_id(policy_id);
  curr_txn.set_policy_weight(policy_weight);
  std::string txn_data;
  curr_txn.SerializeToString(&txn_data);
  currTxnState.set_txn_data(txn_data);

  currTxnState.SerializeToString(&txnState);
}

}
