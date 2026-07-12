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

#ifndef _CLIENT_SELECTOR_H_
#define _CLIENT_SELECTOR_H_

#include <vector>
#include <random>


// used to select clients to satisfy a policy that does not specify a specific client id
class ClientSelector {
 public:
  ClientSelector(uint64_t client_total) : client_total(client_total) {};
  virtual ~ClientSelector() {}

  virtual uint64_t GetClientId(std::mt19937 &rand) = 0;
  virtual std::vector<uint64_t> GetClientIds(std::mt19937 &rand, uint64_t num_clients, bool with_replacement=true) = 0;

 protected:
  const uint64_t client_total;

};

#endif /* _CLIENT_SELECTOR_H_ */
