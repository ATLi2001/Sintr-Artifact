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

#include "store/common/policy/or_policy.h"
#include "store/common/policy/policy-proto.pb.h"
#include "lib/assert.h"

#include <algorithm>


ORPolicy::ORPolicy(const std::set<uint64_t> &client_ids) : 
  client_ids(client_ids) {}
ORPolicy::ORPolicy(const ORPolicy &other) : client_ids(other.client_ids) {}

void ORPolicy::operator= (const ORPolicy &other) {
  client_ids = other.client_ids;
}

bool ORPolicy::operator== (const ORPolicy &other) const {
  return client_ids == other.client_ids;
}
bool ORPolicy::operator!= (const ORPolicy &other) const {
  return !(*this == other);
}
bool ORPolicy::operator< (const ORPolicy &other) const {
  return (*this <= other) && (*this != other);
}
bool ORPolicy::operator> (const ORPolicy &other) const {
  return other < *this;
}
bool ORPolicy::operator<= (const ORPolicy &other) const {
  // this client ids is a superset of other client ids
  // this policy has more OR client ids, so is strictly easier to satisfy
  return std::includes(
    client_ids.begin(), client_ids.end(),
    other.client_ids.begin(), other.client_ids.end()
  );
}
bool ORPolicy::operator>= (const ORPolicy &other) const {
  return other <= *this;
}

PolicyType ORPolicy::Type() const {
  return type;
}

Policy *ORPolicy::Clone() const {
  return new ORPolicy(*this);
}

std::set<uint64_t> ORPolicy::GetORList() const {
  return client_ids;
}

bool ORPolicy::IsSatisfied(const std::set<uint64_t> &endorsements) const {
  // empty OR policy is satisfied by any endorsements
  if (client_ids.empty()) {
    return true;
  }

  for (const auto &client_id : client_ids) {
    // at least one client id is satisfied
    if (endorsements.find(client_id) != endorsements.end()) {
      return true;
    }
  }
  return false;
}

void ORPolicy::MergePolicy(const Policy *other) {
  UW_ASSERT(other != nullptr);
  UW_ASSERT(type == other->Type());

  const ORPolicy *otherORPolicy = static_cast<const ORPolicy *>(other);
  std::set<uint64_t> other_client_ids = otherORPolicy->GetORList();

  if (*this >= *otherORPolicy) {
    // nothing to merge
    return;
  }
  else if (*this <= *otherORPolicy) {
    client_ids = other_client_ids;
    return;
  }
  else {
    // disallow merge two OR policies into a CNF with multiple clauses
    Panic("Cannot merge OR policies that result in CNF with multiple clauses");
  }
}

std::vector<int> ORPolicy::DifferenceToSatisfied(
    const std::set<uint64_t> &potentialEndorsements) const {
  std::vector<int> ret;

  if (IsSatisfied(potentialEndorsements)) {
    // already satisfied
    return ret;
  }
  // otherwise return a random client id from OR
  int offset = std::rand() % client_ids.size();
  auto it = client_ids.begin();
  std::advance(it, offset);
  ret.push_back(*it);

  return ret;
}

bool ORPolicy::IsImpliedBy(const Policy *other) const {
  UW_ASSERT(other != nullptr);
  UW_ASSERT(type == other->Type());

  const ORPolicy *otherORPolicy = static_cast<const ORPolicy *>(other);
  return *otherORPolicy >= *this;
}

void ORPolicy::SerializeToProtoMessage(PolicyObject *msg) const {
  ORPolicyMessage ORPolicyMsg;
  for (const auto &client_id : client_ids) {
    ORPolicyMsg.add_client_ids(client_id);
  }
  msg->set_policy_type(PolicyObject::OR_POLICY);
  ORPolicyMsg.SerializeToString(msg->mutable_policy_data());
}

void ORPolicy::Reset() {
  client_ids.clear();
}

std::string ORPolicy::ToString() const {
  std::string ret = GetPolicyTypeName(type);
  for (const auto &client_id : client_ids) {
    ret +=  " " + std::to_string(client_id);
  }
  return ret;
}
