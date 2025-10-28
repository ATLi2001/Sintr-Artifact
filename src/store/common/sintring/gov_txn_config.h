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

#ifndef GOV_TXN_CONFIG_H
#define GOV_TXN_CONFIG_H

#include "lib/message.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

// for now, only support changing policy weights
struct GovTxnConfig {
  GovTxnConfig() {}
  GovTxnConfig(std::string configPath) {
    std::ifstream configFile(configPath);
    if (configFile.fail()) {
      Panic("Cannot open gov txn config file %s", configPath.c_str());
    }
    std::string line;
    while (std::getline(configFile, line)) {
      // expected format is "time(s),policy_id,new_policy_weight"
      std::istringstream iss(line);
      std::string temp;
      int i = 0;
      while (std::getline(iss, temp, ',')) {
        if (i == 0) {
          policyChangeTimes.push_back(std::stoull(temp));
        } else if (i == 1) {
          policyChangeIds.push_back(std::stoull(temp));
        } else {
          newPolicyWeights.push_back(std::stoul(temp));
        }
        i++;
      }
    }
  }

  std::vector<uint64_t> policyChangeTimes; // times (in seconds) at which to change policies
  std::vector<uint64_t> policyChangeIds; // tables whose policies will be changed
  std::vector<uint32_t> newPolicyWeights; // new weights for the changed policies
};

#endif /* GOV_TXN_CONFIG_H */
