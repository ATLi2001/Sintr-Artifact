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

#include "store/common/policy/policy_cache.h"
#include "lib/assert.h"
#include "lib/message.h"


bool PolicyCache::IsEmpty() const {
  return policyCache.empty();
}

const Policy *PolicyCache::Get(const std::string &policyId) const {
  auto it = policyCache.find(policyId);
  if (it == policyCache.end()) {
    return nullptr;
  }
  return it->second.second.get();
}

const Timestamp PolicyCache::GetTimestamp(const std::string &policyId) const {
  auto it = policyCache.find(policyId);
  if (it == policyCache.end()) {
    return Timestamp(); // default return 0.0
  }
  return it->second.first;
}

std::unique_ptr<Policy> PolicyCache::Take(const std::string &policyId) {
  auto it = policyCache.find(policyId);
  if (it == policyCache.end()) {
    return nullptr;
  }
  std::unique_ptr<Policy> policy = std::move(it->second.second);
  policyCache.erase(it);
  return policy;
}

void PolicyCache::Put(const std::string &policyId, std::unique_ptr<Policy> policy, Timestamp timestamp) {
  UW_ASSERT(policy != nullptr);
  // unique_ptr automatically handles cleanup of existing policy
  policyCache[policyId] = std::make_pair(timestamp, std::move(policy));
}

std::vector<std::string> PolicyCache::GetAllKeys() const {
  std::vector<std::string> keys;
  for (const auto &entry : policyCache) {
    keys.push_back(entry.first);
  }
  return keys;
}
