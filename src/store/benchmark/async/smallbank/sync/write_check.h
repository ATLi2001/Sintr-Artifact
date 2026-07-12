/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Matthew Burke <matthelb@cs.cornell.edu>
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
#ifndef SYNC_WRITE_CHECK_H
#define SYNC_WRITE_CHECK_H

#include "store/benchmark/async/smallbank/write_check.h"
#include "store/benchmark/async/smallbank/smallbank-validation-proto.pb.h"
#include "store/common/frontend/sync_client.h"
#include "store/benchmark/async/smallbank/sync/smallbank_transaction.h"

namespace smallbank {

class SyncWriteCheck : public SyncSmallbankTransaction, public WriteCheck  {
 public:
  SyncWriteCheck(const std::string &cust, const int32_t value, const uint32_t timeout,
    bool bftsmart_exec_txn_server_side);
  virtual ~SyncWriteCheck();

  transaction_status_t Execute(SyncClient &client);
};

} // namespace smallbank

#endif /* SYNC_WRITE_CHECK_H */