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

#include "store/common/policy/zipf_client_selector.h"

#include <algorithm>


ZipfClientSelector::ZipfClientSelector(uint64_t client_total, double zipfianconstant) 
    : ClientSelector(client_total), zipfianconstant(zipfianconstant), uniform_dist(0.0, 1.0) {
  zetan = harmonic_number(client_total, zipfianconstant);
  for (uint64_t i = 1; i <= client_total; ++i) {
    probabilities.push_back((1.0 / std::pow(i, zipfianconstant)) / zetan);
  }
}

uint64_t ZipfClientSelector::GetClientId(std::mt19937 &rand) {
  return sample_client_from_distribution(rand, probabilities);
}

std::vector<uint64_t> ZipfClientSelector::GetClientIds(std::mt19937 &rand, uint64_t num_clients, bool with_replacement) {
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
    if (num_clients >= client_total) {
      std::vector<uint64_t> all_client_ids(client_total);
      for (uint64_t i = 0; i < client_total; ++i) {
        all_client_ids[i] = i;
      }
      return all_client_ids;
    }

    std::vector<double> prob_copy = probabilities;
    for (uint64_t i = 0; i < num_clients; ++i) {
      uint64_t client_id = sample_client_from_distribution(rand, prob_copy);
      client_ids.push_back(client_id);
      // renormalize probabilities
      double selected_prob = prob_copy[client_id];
      prob_copy[client_id] = 0.0;
      for (double &prob : prob_copy) {
        prob /= (1 - selected_prob);
      }
    }
  }

  return client_ids;
}

double ZipfClientSelector::harmonic_number(uint64_t n, double theta) {
  double sum = 0.0;
  for (uint64_t i = 1; i <= n; ++i) {
      sum += 1.0 / std::pow(i, theta);
  }
  return sum;
}

uint64_t ZipfClientSelector::sample_client_from_distribution(std::mt19937 &rand, std::vector<double> &probabilities) {
  double random_value = uniform_dist(rand);
  double cumulative_probability = 0.0;

  for (uint64_t i = 0; i < probabilities.size(); ++i) {
    cumulative_probability += probabilities[i];
    if (random_value < cumulative_probability) {
      return i;
    }
  }

  // Fallback in case of rounding errors
  return probabilities.size() - 1;
}
