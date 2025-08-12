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

#include "store/benchmark/async/rw-sql/validation/rw-sql_val_transaction.h"


namespace rwsql {

RWSQLValTransaction::RWSQLValTransaction(uint32_t timeout, std::mt19937 &rand, const validation::proto::RWSql &msg) 
    : ValidationTransaction(timeout), liveOps(msg.num_ops()), RWSQLBaseTransaction(msg.num_ops(), msg.read_secondary_condition(),
    msg.num_keys(), msg.value_size(), msg.value_categories(), rand, msg.read_only(), msg.scan_as_point(), msg.exec_point_scan_parallel())
{
  for(const int32_t &i : msg.tables()) {
    tables.push_back(i);
  }
  for(const int32_t &i : msg.starts()) {
    starts.push_back(i);
  }
  for(const int32_t &i : msg.ends()) {
    ends.push_back(i);
  }
  for (auto const &i : msg.secondary_values()) {
    secondary_values.push_back(std::make_pair(i.first(), i.second()));
  }
}

RWSQLValTransaction::~RWSQLValTransaction() {
}

static int count = 1;

//WARNING: CURRENTLY DO NOT SUPPORT READ YOUR OWN WRITES
transaction_status_t RWSQLValTransaction::Validate(SyncClient &client) {
  //Note: Semantic CC cannot help this Transaction avoid aborts. Since it does value++, all TXs that touch value must be totally ordered. 
  
  //reset Tx exec state. When avoiding redundant queries we may split into new queries. liveOps keeps track of total number of attempted queries
  statements.clear();
  
  return RWSQLBaseTransaction::BaseExecute(client, timeout, false, liveOps, numKeys);
}

} // namespace rwsql
