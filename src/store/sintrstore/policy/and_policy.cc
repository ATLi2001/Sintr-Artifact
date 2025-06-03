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

#include "store/sintrstore/policy/and_policy.h"
#include "store/sintrstore/policy/policy-proto.pb.h"
#include "lib/assert.h"

#include <algorithm>

namespace sintrstore {

ANDPolicy::ANDPolicy(const std::set<uint64_t> &client_ids) : 
  client_ids(client_ids) {}
ANDPolicy::ANDPolicy(const ANDPolicy &other) : client_ids(other.client_ids) {}

void ANDPolicy::operator= (const ANDPolicy &other) {
  client_ids = other.client_ids;
}

bool ANDPolicy::operator== (const ANDPolicy &other) const {
  return client_ids == other.client_ids;
}
bool ANDPolicy::operator!= (const ANDPolicy &other) const {
  return !(*this == other);
}
bool ANDPolicy::operator< (const ANDPolicy &other) const {
  return (*this <= other) && (*this != other);
}
bool ANDPolicy::operator> (const ANDPolicy &other) const {
  return other < *this;
}
bool ANDPolicy::operator<= (const ANDPolicy &other) const {
  // this and list a subset of other and list
  return std::includes(
    other.client_ids.begin(), other.client_ids.end(), 
    client_ids.begin(), client_ids.end()
  );
}
bool ANDPolicy::operator>= (const ANDPolicy &other) const {
  return other <= *this;
}

PolicyType ANDPolicy::Type() const {
  return type;
}

Policy *ANDPolicy::Clone() const {
  return new ANDPolicy(*this);
}

std::set<uint64_t> ANDPolicy::GetANDList() const {
  return client_ids;
}

bool ANDPolicy::IsSatisfied(const std::set<uint64_t> &endorsements) const {
  return std::includes(
    endorsements.begin(), endorsements.end(), 
    client_ids.begin(), client_ids.end()
  );
}

void ANDPolicy::MergePolicy(const Policy *other) {
  UW_ASSERT(other != nullptr);
  UW_ASSERT(type == other->Type());

  const ANDPolicy *otherANDPolicy = static_cast<const ANDPolicy *>(other);
  std::set<uint64_t> other_client_ids = otherANDPolicy->GetANDList();
  client_ids.insert(other_client_ids.begin(), other_client_ids.end());
}

std::vector<int> ANDPolicy::DifferenceToSatisfied(
    const std::set<uint64_t> &potentialEndorsements) const {
  std::vector<int> ret;

  for (const auto &client_id : client_ids) {
    if (potentialEndorsements.find(client_id) == potentialEndorsements.end()) {
      ret.push_back(client_id);
    }
  }

  return ret;
}

bool ANDPolicy::IsImpliedBy(const Policy *other) const {
  UW_ASSERT(other != nullptr);
  UW_ASSERT(type == other->Type());

  const ANDPolicy *otherANDPolicy = static_cast<const ANDPolicy *>(other);
  return *otherANDPolicy >= *this;
}

void ANDPolicy::SerializeToProtoMessage(proto::PolicyObject *msg) const {
  proto::ANDPolicyMessage ANDPolicyMsg;
  for (const auto &client_id : client_ids) {
    ANDPolicyMsg.add_client_ids(client_id);
  }
  msg->set_policy_type(proto::PolicyObject::AND_POLICY);
  ANDPolicyMsg.SerializeToString(msg->mutable_policy_data());
}

void ANDPolicy::Reset() {
  client_ids.clear();
}

std::string ANDPolicy::ToString() const {
  std::string ret = GetPolicyTypeName(type);
  for (const auto &client_id : client_ids) {
    ret +=  " " + std::to_string(client_id);
  }
  return ret;
}

} // namespace sintrstore
