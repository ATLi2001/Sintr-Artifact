/***********************************************************************
 *
 * Copyright 2025 Daniel Lee <dhl93@cornell.edu>
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

#include "store/common/sintring/client_common.h"
#include "store/common/policy/policy_id.h"
#include "lib/message.h"


bool IsPolicyChangeTxn(const TxnState &protoTxnState) {
  return protoTxnState.txn_name().find("policy") != std::string::npos;
}

void EstimateTxnPolicy(const TxnState &protoTxnState, PolicyClient *policyClient, const PolicyCache &policyCache, SintrParameters sintr_params) {
  if (IsPolicyChangeTxn(protoTxnState)) {
    // policy change transaction could require separate handling
    const Policy *policy = policyCache.Get(PolicyIdString(0));
    if(policy == nullptr) {
      Panic("Policy for policy id 0 not found in policy cache");
    }
    policyClient->AddPolicy(policy);
  } 
  else {
    EstimatePolicy est_policy_obj(sintr_params.includeReadsetForTxnPolicy, sintr_params.policyFunctionName);
    est_policy_obj.EstimateTxnPolicy(protoTxnState, policyClient, policyCache);
  }
}
