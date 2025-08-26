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


PolicyCache::PolicyCache() {}

PolicyCache::~PolicyCache() {
  for (const auto &idPolicy : policyCache) {
    delete idPolicy.second;
  }
}

void PolicyCache::Initialize(std::map<std::string, Policy *> &&policies) {
  UW_ASSERT(this->policyCache.empty());
  policyCache = std::move(policies);
}

bool PolicyCache::IsEmpty() const {
  return policyCache.empty();
}

bool PolicyCache::Get(const std::string &policyId, const Policy *&policy) const {
  auto it = policyCache.find(policyId);
  if (it == policyCache.end()) {
    return false;
  }
  policy = it->second;
  return true;
}

void PolicyCache::Put(const std::string &policyId, Policy *&&policy) {
  UW_ASSERT(policy != nullptr);
  if (policyCache.find(policyId) != policyCache.end()) {
    delete policyCache[policyId];
  }
  policyCache.emplace(policyId, std::move(policy));
}
