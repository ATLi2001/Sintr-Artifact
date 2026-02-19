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
#include "store/benchmark/async/sql/tpcc-lifting/tpcc_lifts.h"
#include "store/common/policy/policy_id.h"
#include "store/common/policy/policy_types.h"
#include "store/common/policy/weight_policy.h"
#include <cmath>

namespace tpcc_lift_sql {

bool TPCCLifts::IsLiftedPolicyFunction() const {
  return policy_function_name == "tpcc_lift_payment_stock";
}

std::vector<std::string> TPCCLifts::DeliveryLiftFunction(const PolicyCache &policy_cache, const std::map<std::string, std::string> &readset,
  const std::vector<int32_t> &total_amts, const std::vector<int32_t> &customer_amts) const {
  std::vector<std::string> lifted_keys;
  // just lift transaction as long as we are using tpcc_lift_payment_stock policy function
  if (!IsLiftedPolicyFunction()) {
    return lifted_keys;
  }
  UW_ASSERT(customer_amts.size() == total_amts.size());
  for(int i = 0; i < customer_amts.size(); i++) {
    if(customer_amts[i] < total_amts[i]) {
      // don't lift if total amount more is less than customer can afford
      return lifted_keys;
    }
  }
  // otherwise lift entire readset
  for (auto const& [key, val] : readset) {
    lifted_keys.push_back(key);
  }

  return lifted_keys;
}
}
