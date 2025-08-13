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
#include "store/benchmark/async/tpcc/validation/payment.h"
#include "store/benchmark/async/tpcc/tpcc_utils.h"

#include <sstream>
  
namespace tpcc {

ValidationPayment::ValidationPayment(uint32_t timeout, uint32_t w_id, uint32_t d_id, uint32_t d_w_id, uint32_t c_w_id,
    uint32_t c_d_id, uint32_t c_id, uint32_t h_amount, uint32_t h_date, bool c_by_last_name, std::string c_last) :
    ValidationTPCCTransaction(timeout) {
  this->w_id = w_id;
  this->d_id = d_id;
  this->d_w_id = d_w_id;
  this->c_w_id = c_w_id;
  this->c_d_id = c_d_id;
  this->c_id = c_id;
  this->h_amount = h_amount;
  this->h_date = h_date;
  this->c_by_last_name = c_by_last_name;
  this->c_last = c_last;
}
ValidationPayment::ValidationPayment(uint32_t timeout, const validation::proto::Payment &valPaymentMsg) : 
    ValidationTPCCTransaction(timeout) {
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
}

ValidationPayment::~ValidationPayment() {
}

transaction_status_t ValidationPayment::Validate(::SyncClient &client) {
    return Payment::BaseExecute(client, timeout, false);
}

} // namespace tpcc
