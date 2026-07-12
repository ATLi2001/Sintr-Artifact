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
#include "store/benchmark/async/tpcc/validation/stock_level.h"
#include "store/benchmark/async/tpcc/tpcc_utils.h"

#include <map>

namespace tpcc {

ValidationStockLevel::ValidationStockLevel(uint32_t timeout, uint32_t w_id, uint32_t d_id, uint8_t min_quantity) :
    ValidationTPCCTransaction(timeout) {
  this->w_id = w_id;
  this->d_id = d_id;
  this->min_quantity = min_quantity;
}

ValidationStockLevel::ValidationStockLevel(uint32_t timeout, const validation::proto::StockLevel &valStockLevelMsg) : 
    ValidationTPCCTransaction(timeout) {
  w_id = valStockLevelMsg.w_id();
  d_id = valStockLevelMsg.d_id();
  // protobuf only has uint32 type, but here we only need uint8_t
  min_quantity = valStockLevelMsg.min_quantity() & 0xFF;
}

ValidationStockLevel::~ValidationStockLevel() {
}

transaction_status_t ValidationStockLevel::Validate(::SyncClient &client) {
  return StockLevel::BaseExecute(client, timeout, false);
}

} // namespace tpcc
