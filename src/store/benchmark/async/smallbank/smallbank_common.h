/***********************************************************************
 *
 * Copyright 2024 Daniel Lee <dhl93@cornell.edu>
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
#ifndef SMALLBANK_COMMON_H
#define SMALLBANK_COMMON_H

#include "lib/message.h"
#include "store/benchmark/async/smallbank/smallbank_client.h"
#include "store/benchmark/async/smallbank/smallbank_transaction.h"

#include <string>

namespace smallbank {

const std::string BENCHMARK_NAME = "smallbank";

inline std::string GetBenchmarkTxnTypeName(SmallbankTransactionType txn_type) {
  switch (txn_type) {
    case BALANCE:
      return "balance";
    case DEPOSIT:
      return "deposit";
    case TRANSACT:
      return "transact";
    case AMALGAMATE:
      return "amalgamate";
    case WRITE_CHECK:
      return "write_check";
    default:
      Panic("Received unexpected txn type: %d", txn_type);
  }
}

inline SmallbankTransactionType GetBenchmarkTxnTypeEnum(std::string &txn_type) {
  if (txn_type == "balance") {
    return BALANCE;
  }
  else if (txn_type == "deposit") {
    return DEPOSIT;
  }
  else if (txn_type == "transact") {
    return TRANSACT;
  }
  else if (txn_type == "amalgamate") {
    return AMALGAMATE;
  }
  else if (txn_type == "write_check") {
    return WRITE_CHECK;
  }
  else {
    Panic("Received unexpected txn type: %s", txn_type.c_str());
  }
}

}

#endif /* SMALLBANK_COMMON_H */
