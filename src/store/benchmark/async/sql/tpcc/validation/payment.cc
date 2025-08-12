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

#include "store/benchmark/async/sql/tpcc/validation/payment.h"
#include "store/benchmark/async/sql/tpcc/tpcc_utils.h"


namespace tpcc_sql {

ValidationSQLPayment::ValidationSQLPayment(uint32_t timeout, std::mt19937 &gen, const validation::proto::Payment &valPaymentMsg) : 
  ValidationTPCCSQLTransaction(timeout), SQLPayment(gen) {
  w_id = valPaymentMsg.w_id();
  d_id = valPaymentMsg.d_id();
  d_w_id = valPaymentMsg.d_w_id();
  c_w_id = valPaymentMsg.c_w_id();
  c_d_id = valPaymentMsg.c_d_id();
  c_id = valPaymentMsg.c_id();
  h_amount = valPaymentMsg.h_amount();
  h_date = valPaymentMsg.h_date();
  c_by_last_name = valPaymentMsg.c_by_last_name();
  c_last = valPaymentMsg.c_last();
  random_row_id = valPaymentMsg.random_row_id();
}

ValidationSQLPayment::~ValidationSQLPayment() {
}

transaction_status_t ValidationSQLPayment::Validate(SyncClient &client) {
  return SQLPayment::BaseExecute(client, timeout, false);
}

} // namespace tpcc_sql
