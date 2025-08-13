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
#include "store/benchmark/async/tpcc/validation/delivery.h"
#include "store/benchmark/async/tpcc/tpcc_utils.h"
  
namespace tpcc {

ValidationDelivery::ValidationDelivery(uint32_t timeout, uint32_t w_id, uint32_t d_id, 
    uint32_t o_carrier_id, uint32_t ol_delivery_d) : ValidationTPCCTransaction(timeout) {
  this->w_id = w_id;
  this->d_id = d_id; 
  this->o_carrier_id = o_carrier_id;
  this->ol_delivery_d = ol_delivery_d;
}

ValidationDelivery::ValidationDelivery(uint32_t timeout, const validation::proto::Delivery &valDeliveryMsg) : 
    ValidationTPCCTransaction(timeout) {
  w_id = valDeliveryMsg.w_id();
  d_id = valDeliveryMsg.d_id();
  o_carrier_id = valDeliveryMsg.o_carrier_id();
  ol_delivery_d = valDeliveryMsg.ol_delivery_d();
}

ValidationDelivery::~ValidationDelivery() {
}

transaction_status_t ValidationDelivery::Validate(::SyncClient &client) {
  return Delivery::BaseExecute(client, timeout, false);
}

} // namespace tpcc
