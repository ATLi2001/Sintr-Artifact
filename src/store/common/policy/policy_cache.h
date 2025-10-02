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

#ifndef _POLICY_CACHE_H_
#define _POLICY_CACHE_H_

#include "store/common/policy/policy.h"
#include <map>
#include <string>
#include <memory>

class PolicyCache {
 public:
  PolicyCache() {};
  ~PolicyCache() {};

  // is empty
  bool IsEmpty() const;
  // return true if policy exists for key, false otherwise
  // given a reference to a policy pointer, update it with the policy in the cache
  // does not allocate a new policy object
  const Policy *Get(const std::string &policyId) const;
  // take ownership and remove from underlying map
  std::unique_ptr<Policy> Take(const std::string &policyId);
  // update the mapping from policy id to policy; takes ownership of policy (policy should be allocated on heap)
  void Put(const std::string &policyId, std::unique_ptr<Policy> policy);
  // return all keys
  std::vector<std::string> GetAllKeys() const;

 private:
  std::map<std::string, std::unique_ptr<Policy>> policyCache;
};

#endif /* _POLICY_CACHE_H_ */
