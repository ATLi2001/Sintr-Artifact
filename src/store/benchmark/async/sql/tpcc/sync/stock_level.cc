/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Matthew Burke <matthelb@cs.cornell.edu>
 *                Liam Arzola <lma77@cornell.edu>
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

#include "store/benchmark/async/sql/tpcc/sync/stock_level.h"
#include "store/benchmark/async/sql/tpcc/tpcc_utils.h"


namespace tpcc_sql {

SyncSQLStockLevel::SyncSQLStockLevel(uint32_t timeout, uint32_t w_id, uint32_t d_id,
    std::mt19937 &gen, bool bftsmart_exec_txn_server_side) : SyncTPCCSQLTransaction(timeout, bftsmart_exec_txn_server_side), SQLStockLevel(w_id, d_id, gen) {
}

SyncSQLStockLevel::~SyncSQLStockLevel() {
}

transaction_status_t SyncSQLStockLevel::Execute(SyncClient &client) {
  return SQLStockLevel::BaseExecute(client, timeout, true, bftsmart_exec_txn_server_side);
}

} // namespace tpcc_sql
