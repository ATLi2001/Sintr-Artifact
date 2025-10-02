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

#include "store/benchmark/async/rw-sql/validation/rw-sql_val_policy_change.h"


namespace rwsql {

RWSQLValPolicyChange::RWSQLValPolicyChange(uint32_t timeout, const validation::proto::RWSqlPolicyChange &msg,
  const std::string &policy_function_name) 
  : ValidationTransaction(timeout), RWSQLBasePolicyChange(msg.table(), msg.policy_weight(), policy_function_name) {}

RWSQLValPolicyChange::~RWSQLValPolicyChange() {}

transaction_status_t RWSQLValPolicyChange::Validate(SyncClient &client) {
  return RWSQLBasePolicyChange::BaseExecute(client, timeout, false);
}

}
