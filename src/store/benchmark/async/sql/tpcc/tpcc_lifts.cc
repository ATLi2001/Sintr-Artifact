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

#include "lib/assert.h"
#include "store/benchmark/async/sql/tpcc/tpcc_lifts.h"
#include "store/common/policy/policy_id.h"
#include "store/common/policy/policy_types.h"
#include "store/common/policy/weight_policy.h"
#include <cmath>

namespace tpcc_sql {

bool TPCCLifts::IsLiftedPolicyFunction() const {
  return policy_function_name == "tpcc_sql_wh";
}

bool TPCCLifts::NewOrderLiftFunction(const PolicyCache &policy_cache) const {
  if (!IsLiftedPolicyFunction()) {
    return false;
  }

  // for now always lift
  return true;
}

bool TPCCLifts::PaymentLiftFunction(const PolicyCache &policy_cache, uint32_t w_id, uint32_t c_w_id, uint32_t h_amount) const {
  if (!IsLiftedPolicyFunction()) {
    return false;
  }

  // only lift if customer is paying not their home warehouse
  if (w_id == c_w_id) {
    return false;
  }

  const Policy *policy = policy_cache.Get(PolicyIdString(c_w_id));
  UW_ASSERT(policy != nullptr);

  // for now only lift if weight policy
  if (policy->Type() != PolicyType::POLICY_TYPE_WEIGHT) {
    return false;
  }

  // cast to weight policy
  const WeightPolicy *weight_policy = dynamic_cast<const WeightPolicy *>(policy);
  UW_ASSERT(weight_policy != nullptr);

  // lifting rule is threshold based on weight
  // h_amount is between 100 and 500000
  // assuming weight is small and at least 2
  // uint64_t threshold = static_cast<uint64_t>(std::pow(10, weight_policy->GetWeight() + 1));
  // return h_amount <= threshold;
  if (weight_policy->GetWeight() == 1) {
    return false;
  }
  else {
    return true;
  }
}

}
