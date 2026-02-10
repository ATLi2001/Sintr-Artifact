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

#include "store/benchmark/async/sql/tpcc/validation/new_order.h"
#include "store/benchmark/async/sql/tpcc/tpcc_utils.h"


namespace tpcc_sql {

ValidationSQLNewOrder::ValidationSQLNewOrder(uint32_t timeout, const validation::proto::NewOrder &valNewOrderMsg,
    const TPCCLifts &tpcc_lifts) : ValidationTPCCSQLTransaction(timeout) {
  w_id = valNewOrderMsg.w_id();
  d_id = valNewOrderMsg.d_id();
  c_id = valNewOrderMsg.c_id();
  ol_cnt = valNewOrderMsg.ol_cnt();
  rbk = valNewOrderMsg.rbk();
  o_ol_i_ids = std::vector(valNewOrderMsg.o_ol_i_ids().begin(), valNewOrderMsg.o_ol_i_ids().end());
  o_ol_supply_w_ids = std::vector(valNewOrderMsg.o_ol_supply_w_ids().begin(), valNewOrderMsg.o_ol_supply_w_ids().end());
  // protobuf only has uint32 type, but here we only need uint8_t in the vector
  o_ol_quantities = std::vector<uint8_t>();
  for (int i = 0; i < valNewOrderMsg.o_ol_quantities().size(); i++) {
    o_ol_quantities.push_back(valNewOrderMsg.o_ol_quantities(i) & 0xFF);
  }
  unique_items = std::set(valNewOrderMsg.unique_items().begin(), valNewOrderMsg.unique_items().end());
  o_entry_d = valNewOrderMsg.o_entry_d();
  all_local = valNewOrderMsg.all_local();
  this->tpcc_lifts = tpcc_lifts;
}

ValidationSQLNewOrder::~ValidationSQLNewOrder() {
} 
 
transaction_status_t ValidationSQLNewOrder::Validate(SyncClient &client) {
  return SQLNewOrder::BaseExecute(client, timeout, false);
}

} // namespace tpcc_sql
