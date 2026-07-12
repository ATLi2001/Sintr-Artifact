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

#ifndef _VALIDATION_CLIENT_COMMON_H_
#define _VALIDATION_CLIENT_COMMON_H_

#include "store/common/frontend/client.h"
#include <string>
#include <thread>
#include "tbb/concurrent_hash_map.h"


class ValidationClientCommon : public Client {
public:

  ValidationClientCommon() {};
  virtual ~ValidationClientCommon() {};

  // Set the current transaction id (client that initiated and seq num)
  // associate transaction id with current thread id
  void SetThreadValTxnId(uint64_t txn_client_id, uint64_t txn_client_seq_num);

protected:
  // read from threadValTxnIds and set the passed in references to the current threads txn id
  void GetThreadValTxnId(uint64_t &txn_client_id, uint64_t &txn_client_seq_num);
  std::string ToTxnId(uint64_t txn_client_id, uint64_t txn_client_seq_num);

private:
  // map from thread id to (txn_client_id, txn_client_seq_num) tracks what each thread is doing
  // TODO: Change to a regular map instead of a concurrent hash map because the keys are thread IDs
  typedef tbb::concurrent_hash_map<std::thread::id, std::pair<uint64_t, uint64_t>> threadValTxnIdsMap;
  threadValTxnIdsMap threadValTxnIds;
};

#endif /* _VALIDATION_CLIENT_COMMON_H_ */
