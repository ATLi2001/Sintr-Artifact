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
#include "store/benchmark/async/smallbank/validation/write_check.h"
#include "store/benchmark/async/smallbank/utils.h"

namespace smallbank {

ValidationWriteCheck::ValidationWriteCheck(const std::string &cust, const int32_t value, const uint32_t timeout) :
    ValidationSmallbankTransaction(timeout), WriteCheck(cust, value, timeout) {}

ValidationWriteCheck::ValidationWriteCheck(const validation::proto::WriteCheck &valWriteCheckMsg, const uint32_t timeout)
    : ValidationSmallbankTransaction(timeout), WriteCheck(valWriteCheckMsg.cust(), valWriteCheckMsg.value(), timeout) {}

ValidationWriteCheck::~ValidationWriteCheck() {
}
transaction_status_t ValidationWriteCheck::Validate(SyncClient &client) {
    return WriteCheck::BaseExecute(client, false);
}

} // namespace smallbank
