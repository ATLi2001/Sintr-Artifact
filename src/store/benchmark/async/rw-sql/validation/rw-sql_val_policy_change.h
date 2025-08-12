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

#ifndef RW_SQL_VAL_POLICY_CHANGE_H
#define RW_SQL_VAL_POLICY_CHANGE_H

#include "store/common/frontend/sync_client.h"
#include "store/common/frontend/validation_transaction.h"
#include "store/benchmark/async/rw-sql/rw-sql_base_policy_change.h"
#include "store/benchmark/async/rw-sql/rw-sql-validation-proto.pb.h"
#include <string>

namespace rwsql {

class RWSQLValPolicyChange : public ValidationTransaction, public RWSQLBasePolicyChange {
 public:
  RWSQLValPolicyChange(uint32_t timeout, const validation::proto::RWSqlPolicyChange &msg);
  ~RWSQLValPolicyChange();

  transaction_status_t Validate(SyncClient &client) override;

  void SerializeTxnState(std::string &txnState);
};

} // namespace rwsql

#endif /* RW_SQL_VAL_POLICY_CHANGE_H */
