/***********************************************************************
 *
 * Copyright 2025 Daniel Lee <dhl93@cornell.edu>
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

#include "store/benchmark/async/sql/tpcc-lifting/validation/order_status.h"
#include "store/benchmark/async/sql/tpcc-lifting/tpcc_utils.h"


namespace tpcc_lift_sql {

ValidationSQLOrderStatus::ValidationSQLOrderStatus(uint32_t timeout, const validation::proto::OrderStatus &valOrderStatusMsg) : 
  ValidationTPCCSQLTransaction(timeout) {
  c_w_id = valOrderStatusMsg.c_w_id();
  c_d_id = valOrderStatusMsg.c_d_id();
  c_id = valOrderStatusMsg.c_id();
  o_id = valOrderStatusMsg.o_id();
  c_by_last_name = valOrderStatusMsg.c_by_last_name();
  c_last = valOrderStatusMsg.c_last();
}

ValidationSQLOrderStatus::~ValidationSQLOrderStatus() {
}

transaction_status_t ValidationSQLOrderStatus::Validate(SyncClient &client) {
  return SQLOrderStatus::BaseExecute(client, timeout, false);
}

} // namespace tpcc_lift_sql
