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

#include "store/sintrstore/policy/policy_parse_client.h"
#include "store/sintrstore/policy/policy_types.h"
#include "store/sintrstore/policy/policy-proto.pb.h"
#include "store/sintrstore/policy/weight_policy.h"
#include "store/sintrstore/policy/and_policy.h"
#include "store/sintrstore/policy/or_policy.h"
#include "lib/message.h"

#include <fstream>
#include <sstream>

namespace sintrstore {

std::map<std::string, Policy *> PolicyParseClient::ParseConfigFile(const std::string &configFilePath) {
  std::map<std::string, Policy *> policies;

  std::ifstream policyStoreFile(configFilePath);
  if (policyStoreFile.fail()) {
    Panic("Cannot open policy store file %s", configFilePath.c_str());
  }

  std::string line;
  while (std::getline(policyStoreFile, line)) {
    // expected format is "policyId policyType args..."
    std::string policyId;
    std::string policyType;
    std::vector<std::string> args;

    // parse line
    std::istringstream iss(line);
    std::string temp;
    int i = 0;
    while (std::getline(iss, temp, ' ')) {
      if (i == 0) {
        policyId = temp;
      } else if (i == 1) {
        policyType = temp;
      } else {
        args.push_back(temp);
      }
      i++;
    }

    // create policy
    Policy *policy = Create(policyType, args);

    // add to policies
    auto result = policies.insert(std::make_pair(policyId, policy));
    if (!result.second) {
      Panic("Policy id %s occurs twice in config file", policyId);
    }
  }

  return policies;
}

Policy *PolicyParseClient::Create(const std::string &policyType, const std::vector<std::string> &policyArgs) {
  switch (GetPolicyTypeEnum(policyType)) {
    case POLICY_TYPE_WEIGHT: {
      if (policyArgs.size() != 1) {
        Panic("Weight policy requires exactly one argument");
      }
      return new WeightPolicy(std::stoull(policyArgs[0]));
    }
    case POLICY_TYPE_AND: {
      std::set<uint64_t> client_ids;
      for (const std::string &arg : policyArgs) {
        client_ids.insert(std::stoull(arg));
      }
      return new ANDPolicy(client_ids);
    }
    case POLICY_TYPE_OR: {
      std::set<uint64_t> client_ids;
      for (const std::string &arg : policyArgs) {
        client_ids.insert(std::stoull(arg));
      }
      return new ORPolicy(client_ids);
    }
    default:
      Panic("Received unexpected policy type: %s", policyType.c_str());
  }
}

Policy *PolicyParseClient::Parse(const proto::PolicyObject &protoPolicy) {
  switch (protoPolicy.policy_type()) {
    case proto::PolicyObject::WEIGHT_POLICY: {
      proto::WeightPolicyMessage weightPolicyMsg;
      weightPolicyMsg.ParseFromString(protoPolicy.policy_data());
      return new WeightPolicy(weightPolicyMsg.weight());
    }
    case proto::PolicyObject::AND_POLICY: {
      proto::ANDPolicyMessage andPolicyMsg;
      andPolicyMsg.ParseFromString(protoPolicy.policy_data());
      std::set<uint64_t> client_ids(andPolicyMsg.client_ids().begin(), andPolicyMsg.client_ids().end());
      return new ANDPolicy(client_ids);
    }
    case proto::PolicyObject::OR_POLICY: {
      proto::ORPolicyMessage orPolicyMsg;
      orPolicyMsg.ParseFromString(protoPolicy.policy_data());
      std::set<uint64_t> client_ids(orPolicyMsg.client_ids().begin(), orPolicyMsg.client_ids().end());
      return new ORPolicy(client_ids);
    }
    default:
      Panic("Received unexpected policy type: %d", protoPolicy.policy_type());
  }
}

} // namespace sintrstore
