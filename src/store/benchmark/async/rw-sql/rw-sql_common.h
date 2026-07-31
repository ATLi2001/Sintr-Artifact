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
#include "store/common/policy/policy_id.h"
#include <string>

namespace rwsql {

const std::string BENCHMARK_NAME = "rwsql";
const uint64_t total_rows = 10000000; // 10 million rows, TODO: make this a parameter for the benchmark
const uint64_t total_rows_per_table = total_rows / 10; // 1 million rows per table, TODO: make this a parameter for the benchmark

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

inline std::string GetPolicyIdForTable(const std::string &table_name, const std::string &policy_function_name = "basic_id",
  const int percentage = 0) {
  if (policy_function_name == "basic_id") {
    return PolicyIdString(0);
  }
  else if (policy_function_name == "rw_sql_policy_change_grouped") {
    if (table_name == "t0") {
      return PolicyIdString(0);
    } 
    else {
      return PolicyIdString(1);
    }
  }
  else if (policy_function_name == "rw_sql_random_policy") {
    // Using this for client side (always estimate policy 0)
    return PolicyIdString(0);
  }
  else if (policy_function_name == "rw_sql_table_based_policy") {
    UW_ASSERT(table_name.size() == 2 && table_name[0] == 't' && std::isdigit(static_cast<unsigned char>(table_name[1])));
    uint32_t table_index = table_name[1] - '0'; // table_name is in the format of "t0", "t1", ..., "t9"
    uint64_t policy_id = ((table_index + 1) * 10 <= percentage) ? 1 : 0;
    return PolicyIdString(policy_id);
  }
  else {
    Panic("Unexpected policy function name for RW-SQL: %s", policy_function_name.c_str());
  }
}


inline std::string GetPolicyIdForTable(const uint64_t table_id, const std::string &policy_function_name = "basic_id", const int percentage = 0) {
  return GetPolicyIdForTable(*DecodeTable(std::to_string(table_id)), policy_function_name, percentage);
}

inline std::string GetPolicyIdForKey(const std::string &key, const std::string &policy_function_name = "rw_sql_random_policy",
  const int percentage = 0) {
  UW_ASSERT(percentage >= 0 && percentage <= 100);
  if(percentage == 0) {
    return PolicyIdString(0);
  } else if (percentage == 100) {
    return PolicyIdString(1);
  }
  else if (policy_function_name == "rw_sql_random_policy") {
    uint64_t unique_int = ParseEncodedKeyToUniqueInt(key, total_rows_per_table);
    uint64_t policy_id = unique_int % 100 < percentage ? 1 : 0;
    return PolicyIdString(policy_id);
  }
  else {
    Panic("Unexpected policy function name for RW-SQL key based: %s", policy_function_name.c_str());
  }
}

}

#endif /* RW_SQL_COMMON_H */
