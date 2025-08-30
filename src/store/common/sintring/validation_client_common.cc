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

#include "store/common/sintring/validation_client_common.h"
#include "lib/message.h"


void ValidationClientCommon::SetThreadValTxnId(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  threadValTxnIdsMap::accessor a;
  threadValTxnIds.insert(a, std::this_thread::get_id());
  a->second = std::make_pair(txn_client_id, txn_client_seq_num);
}

void ValidationClientCommon::GetThreadValTxnId(uint64_t &txn_client_id, uint64_t &txn_client_seq_num) {
  threadValTxnIdsMap::const_accessor a;
  if (!threadValTxnIds.find(a, std::this_thread::get_id())) {
    Panic("Current thread does not validate transactions");
  }

  txn_client_id = a->second.first;
  txn_client_seq_num = a->second.second;
}

std::string ValidationClientCommon::ToTxnId(uint64_t txn_client_id, uint64_t txn_client_seq_num) {
  return std::to_string(txn_client_id) + "_" + std::to_string(txn_client_seq_num);
}
