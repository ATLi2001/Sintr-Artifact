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

#include "store/benchmark/async/rw-sql/sync/rw-sql_transaction.h"


namespace rwsql {

RWSQLTransaction::RWSQLTransaction(QuerySelector *querySelector, uint64_t &numOps, std::mt19937 &rand, bool readSecondaryCondition, bool fixedRange, 
                                     int32_t value_size, uint64_t value_categories, bool readOnly, bool scanAsPoint, bool execPointScanParallel, bool execTxnServerSide,
                                    uint32_t simulatedComputationDelay) 
    : SyncTransaction(10000), RWSQLBaseTransaction(querySelector, numOps, rand, readSecondaryCondition, fixedRange, 
      value_size, value_categories, readOnly, scanAsPoint, execPointScanParallel), execTxnServerSide(execTxnServerSide),
      simulatedComputationDelay(simulatedComputationDelay) {
}

RWSQLTransaction::~RWSQLTransaction() {
}

static int count = 1;

//WARNING: CURRENTLY DO NOT SUPPORT READ YOUR OWN WRITES
transaction_status_t RWSQLTransaction::Execute(SyncClient &client) {
  //Note: Semantic CC cannot help this Transaction avoid aborts. Since it does value++, all TXs that touch value must be totally ordered. 
  
  //reset Tx exec state. When avoiding redundant queries we may split into new queries. liveOps keeps track of total number of attempted queries
  liveOps = numOps;
  statements.clear();
  for (int i = 0; i < numOps; ++i) {
    secondary_values.push_back(GenerateSecondaryCondition());
  }

  return RWSQLBaseTransaction::BaseExecute(client, timeout, true, liveOps, (int) querySelector->numKeys, execTxnServerSide, simulatedComputationDelay);
}

} // namespace rwsql
