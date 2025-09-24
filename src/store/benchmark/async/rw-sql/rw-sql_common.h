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
#ifndef RW_SQL_COMMON_H
#define RW_SQL_COMMON_H

#include "lib/message.h"
#include "store/common/table_kv_encoder.h"
#include <string>

namespace rwsql {

const std::string BENCHMARK_NAME = "rwsql";

enum RWSQLTransactionType {
  RW_SQL_TRANSACTION = 0,
  RW_SQL_POLICY_CHANGE,
  NUM_TXN_TYPES
};

inline std::string GetBenchmarkTxnTypeName(RWSQLTransactionType txn_type) {
  switch (txn_type) {
    case RW_SQL_TRANSACTION:
      return "transaction";
    case RW_SQL_POLICY_CHANGE:
      return "policy_change";
    default:
      Panic("Received unexpected txn type: %d", txn_type);
  }
}

inline RWSQLTransactionType GetBenchmarkTxnTypeEnum(const std::string &txn_type) {
  if (txn_type == "transaction") {
    return RW_SQL_TRANSACTION;
  }
  else if (txn_type == "policy_change") {
    return RW_SQL_POLICY_CHANGE;
  }
  else {
    Panic("Received unexpected txn type: %s", txn_type.c_str());
  }
}

inline std::string GetPolicyIdForTable(const std::string &table_name, const std::string &policy_function_name = "basic_id") {
  if (policy_function_name == "basic_id") {
    return "p#0";
  }
  else if (policy_function_name == "rw_sql_policy_change_grouped") {
    if (table_name == "t0") {
      return "p#0";
    } 
    else {
      return "p#1";
    }
  }
  else {
    Panic("Unexpected policy function name for RW-SQL: %s", policy_function_name.c_str());
  }
}

inline std::string GetPolicyIdForTable(const uint64_t table_id, const std::string &policy_function_name = "basic_id") {
  GetPolicyIdForTable(*DecodeTable(std::to_string(table_id)), policy_function_name);
}

}

#endif /* RW_SQL_COMMON_H */
