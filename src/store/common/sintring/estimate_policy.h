/***********************************************************************
 *
 * Copyright 2024 Daniel Lee <dhl93@cornell.edu>
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

#ifndef POLICY_ESTIMATE_H
#define POLICY_ESTIMATE_H

#include "lib/assert.h"
#include "store/common/common-proto.pb.h"
#include "store/common/frontend/validation_transaction.h"
#include "store/common/policy/policy.h"
#include "store/common/policy/policy_client.h"
#include "store/common/policy/policy_cache.h"


// this class takes TxnState proto message and estimates the policy needed for the transaction
class EstimatePolicy {
 public:
  EstimatePolicy(bool include_readset_for_txn_policy, const std::string &policy_function_name)
      : include_readset_for_txn_policy(include_readset_for_txn_policy), policy_function_name(policy_function_name) {}
  ~EstimatePolicy() {}
  // takes in transaction state, policy, and endorsement client and returns an estimated policy
  void EstimateTxnPolicy(const TxnState &protoTxnState, PolicyClient *policyClient, const PolicyCache &policyCache) const;

 private:
  /*
    As this is a prototype, we'll implement the tables-to-policy ID mapping function in the Sintr client code. 
    Application developers should implement it in their own client code (e.g., tpcc) and then use it within Sintr.
  */
  std::string TableToPolicyID(const uint64_t t, const std::string &txn_bench) const;

  const bool include_readset_for_txn_policy;
  std::string policy_function_name;
};

#endif
