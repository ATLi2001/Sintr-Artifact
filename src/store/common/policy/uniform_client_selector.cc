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

#include "store/common/policy/uniform_client_selector.h"

#include <algorithm>


UniformClientSelector::UniformClientSelector(uint64_t client_total) : ClientSelector(client_total) {}

uint64_t UniformClientSelector::GetClientId(std::mt19937 &rand) {
  return rand() % client_total;
}

std::vector<uint64_t> UniformClientSelector::GetClientIds(std::mt19937 &rand, uint64_t num_clients, bool with_replacement) {
  if (num_clients == 0) {
    return std::vector<uint64_t>();
  }

  std::vector<uint64_t> client_ids;
  if (with_replacement) {
    for (uint64_t i = 0; i < num_clients; ++i) {
      client_ids.push_back(GetClientId(rand));
    }
  }
  else {
    std::vector<uint64_t> all_client_ids(client_total);
    for (uint64_t i = 0; i < client_total; ++i) {
      all_client_ids[i] = i;
    }

    if (num_clients >= client_total) {
      return all_client_ids;
    }
    
    std::shuffle(all_client_ids.begin(), all_client_ids.end(), rand);
    client_ids = std::vector<uint64_t>(all_client_ids.begin(), all_client_ids.begin() + num_clients);
  }

  return client_ids;
}
