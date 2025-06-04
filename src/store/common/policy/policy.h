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

#ifndef _POLICY_H_
#define _POLICY_H_

#include "store/common/policy/policy_types.h"
#include "store/common/common-proto.pb.h"

#include <set>
#include <vector>


// this abstract class represents a generic endorsement policy
// underlying assumption - policies of the same type can be merged
class Policy {
 public:
  Policy() {};
  virtual ~Policy() {};

  // policy type
  virtual PolicyType Type() const = 0;
  // clone a new copy on the heap
  virtual Policy *Clone() const = 0;
  // does endorsements satisfy this Policy object?
  virtual bool IsSatisfied(const std::set<uint64_t> &endorsements) const = 0;
  // merge this Policy with other
  // assume that other is owned by caller, not this policy object
  // other must be of the same type as this policy
  virtual void MergePolicy(const Policy *other) = 0;
  // what client ids does potentialEndorsements need to get this policy satisfied?
  // represent generic client ids with -1
  virtual std::vector<int> DifferenceToSatisfied(const std::set<uint64_t> &potentialEndorsements) const = 0;
  // is this policy implied by other?
  virtual bool IsImpliedBy(const Policy *other) const = 0;
  // serialize to proto version
  virtual void SerializeToProtoMessage(PolicyObject *msg) const = 0;
  virtual void Reset() = 0;
  virtual std::string ToString() const = 0;
};

#endif /* _POLICY_H_ */
