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

#ifndef _SINTR_ENDORSEMENT_AND_POLICY_H_
#define _SINTR_ENDORSEMENT_AND_POLICY_H_

#include "store/sintrstore/policy/policy.h"
#include "store/sintrstore/policy/policy_types.h"
#include "store/sintrstore/sintr-proto.pb.h"

#include <set>

namespace sintrstore {

// this class represents an endorsement policy that is an AND of clients
class ANDPolicy : public Policy {
 public:
  ANDPolicy(const std::set<uint64_t> &client_ids);
  ANDPolicy(const ANDPolicy &other);

  void operator= (const ANDPolicy &other);
  bool operator== (const ANDPolicy &other) const;
  bool operator!= (const ANDPolicy &other) const;
  bool operator< (const ANDPolicy &other) const;
  bool operator> (const ANDPolicy &other) const;
  bool operator<= (const ANDPolicy &other) const;
  bool operator>= (const ANDPolicy &other) const;

  PolicyType Type() const override;
  Policy *Clone() const override;
  std::set<uint64_t> GetANDList() const;
  bool IsSatisfied(const std::set<uint64_t> &endorsements) const override;
  void MergePolicy(const Policy *other) override;
  std::vector<int> DifferenceToSatisfied(const std::set<uint64_t> &potentialEndorsements) const override;
  bool IsImpliedBy(const Policy *other) const override;
  void SerializeToProtoMessage(proto::PolicyObject *msg) const override;
  void Reset() override;
  std::string ToString() const override;

 private:
  // type
  const PolicyType type = POLICY_TYPE_AND;
  // policy is all client ids need to endorse
  std::set<uint64_t> client_ids;
};

} // namespace sintrstore

#endif /* _SINTR_ENDORSEMENT_AND_POLICY_H_ */
