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
#include "store/benchmark/async/smallbank/write_check.h"
#include "store/benchmark/async/smallbank/smallbank_transaction.h"
#include "store/benchmark/async/smallbank/utils.h"
#include "store/benchmark/async/smallbank/smallbank_common.h"
#include "store/benchmark/async/smallbank/smallbank-validation-proto.pb.h"

namespace smallbank {

WriteCheck::WriteCheck(const std::string &cust, const int32_t value, const uint32_t timeout) : SmallbankTransaction(WRITE_CHECK), cust(cust), value(value), timeout(timeout) {}
WriteCheck::~WriteCheck() {
}
transaction_status_t WriteCheck::BaseExecute(SyncClient &client, bool serialize) {
    proto::AccountRow accountRow;
    proto::CheckingRow checkingRow;
    proto::SavingRow savingRow;

    std::string txnState;
    if(serialize) {
        WriteCheck::SerializeTxnState(txnState);
    }

    client.Begin(timeout, txnState);
    Debug("WriteCheck for name %s with value %d", cust.c_str(), value);
    if (!ReadAccountRow(client, cust, accountRow, timeout)) {
        client.Abort(timeout);
        Debug("Aborted WriteCheck (AccountRow)");
        return ABORTED_USER;
    }
    const uint32_t customerId = accountRow.customer_id();
    if (!ReadCheckingRow(client, customerId, checkingRow, timeout) || !
            ReadSavingRow(client, customerId, savingRow, timeout)) {
        client.Abort(timeout);
        Debug("Aborted WriteCheck (Balance)");
        return ABORTED_USER;
    }
    const int32_t sum = checkingRow.checking_balance() + savingRow.saving_balance();
    Debug("Sum for WriteCheck %d", sum);
    if (sum < value) {
        InsertCheckingRow(client, customerId, checkingRow.checking_balance() - value - 1, timeout);
    } else {
        InsertCheckingRow(client, customerId, checkingRow.checking_balance() - value, timeout);
    }
    return client.Commit(timeout);
}

void WriteCheck::SerializeTxnState(std::string &txnState) {
  TxnState currTxnState = TxnState();
  std::string txn_name;
  txn_name.append(BENCHMARK_NAME);
  txn_name.push_back('_');
  txn_name.append(GetBenchmarkTxnTypeName(WRITE_CHECK));
  currTxnState.set_txn_name(txn_name);

  validation::proto::WriteCheck curr_txn = validation::proto::WriteCheck();
  curr_txn.set_cust(cust);
  curr_txn.set_value(value);
  std::string txn_data;
  curr_txn.SerializeToString(&txn_data);
  currTxnState.set_txn_data(txn_data);

  currTxnState.SerializeToString(&txnState);
}

} // namespace smallbank
