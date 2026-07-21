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

#ifndef _POLICY_FUNCTION_H_
#define _POLICY_FUNCTION_H_

#include "store/common/policy/policy.h"
#include "store/common/policy/policy_id.h"
#include "store/benchmark/async/tpcc/tpcc-proto.pb.h"
#include "store/benchmark/async/sql/tpcc/tpcc_schema.h"
#include "store/benchmark/async/sql/tpcc-lifting/tpcc_schema.h"
#include "store/benchmark/async/rw-sql/rw-sql_common.h"
#include "store/common/table_kv_encoder.h"
#include "lib/message.h"

#include <string>


// a policy function takes in the key and value and returns a new policy object
typedef std::function<Policy *(const std::string &, const std::string &)> policy_function;

// a policy id function takes in the key and value and returns a policy id
typedef std::function<std::string(const std::string &, const std::string &)> policy_id_function;

// function that takes in a policy function name and returns the corresponding policy function
inline policy_id_function GetPolicyIdFunction(const std::string &policy_function_name, const int percentage = 0) {
  if (policy_function_name == "basic_id") {
    return [](const std::string &key, const std::string &value) -> std::string {
      return PolicyIdString(0);
    };
  }
  else if (policy_function_name == "grouped") {
    return [](const std::string &key, const std::string &value) -> std::string {
      switch (key.c_str()[0]) {
        case tpcc::Tables::DISTRICT:
          return PolicyIdString(1);
        default:
          return PolicyIdString(0);
      }
    };
  }
  else if (policy_function_name == "rw_sql_policy_change_grouped" || policy_function_name == "rw_sql_table_based_policy") {
    return [policy_function_name, percentage](const std::string &key, const std::string &value) -> std::string {
      std::string table_name;
      std::vector<std::string> primary_key_column_values;
      DecodeTableRow(key, table_name, primary_key_column_values);
      return rwsql::GetPolicyIdForTable(table_name, policy_function_name, percentage);
    };
  }
  else if (policy_function_name == "rw_sql_random_policy") {
    return [policy_function_name, percentage](const std::string &key, const std::string &value) -> std::string {
      return rwsql::GetPolicyIdForKey(key, policy_function_name, percentage);
    };
  }
  // tpcc can support warehouse based policies with lifting
  // 0 is reserved for item table, which is not associated with any warehouse
  // each warehouse id maps to policy id = warehouse id (1-indexed)
  else if (policy_function_name == "tpcc_sql_wh") {
    return [](const std::string &key, const std::string &value) -> std::string {
      Debug("GetPolicyIdFunction: key %s", key.c_str());
      std::string table_name;
      std::vector<std::string> primary_key_column_values;
      DecodeTableRow(key, table_name, primary_key_column_values);

      // warehouse based policy id
      uint32_t w_id = 0;

      if (
        table_name == tpcc_sql::WAREHOUSE_TABLE ||
        table_name == tpcc_sql::DISTRICT_TABLE ||
        table_name == tpcc_sql::CUSTOMER_TABLE ||
        table_name == tpcc_sql::NEW_ORDER_TABLE ||
        table_name == tpcc_sql::ORDER_TABLE ||
        table_name == tpcc_sql::ORDER_LINE_TABLE ||
        table_name == tpcc_sql::EARLIEST_NEW_ORDER_TABLE
      ) {
        w_id = std::stoi(primary_key_column_values[0]);
      }
      else if (table_name == tpcc_sql::STOCK_TABLE) {
        w_id = std::stoi(primary_key_column_values[1]);
      }
      else if (table_name == tpcc_sql::HISTORY_TABLE) {
        w_id = std::stoi(primary_key_column_values[3]);
      }
      else if (table_name == tpcc_sql::ITEM_TABLE) {
        return PolicyIdString(0);
      }
      else {
        Panic("Unknown table name %s", table_name.c_str());
      }

      return PolicyIdString(w_id);
    };
  } else if(policy_function_name == "tpcc_lift_payment_stock") {
        return [](const std::string &key, const std::string &value) -> std::string {
      Debug("GetPolicyIdFunction: key %s", key.c_str());
      std::string table_name;
      std::vector<std::string> primary_key_column_values;
      DecodeTableRow(key, table_name, primary_key_column_values);
      if (
        table_name == tpcc_lift_sql::WAREHOUSE_TABLE ||
        table_name == tpcc_lift_sql::DISTRICT_TABLE ||
        table_name == tpcc_lift_sql::CUSTOMER_TABLE ||
        table_name == tpcc_lift_sql::HISTORY_TABLE
      ) {
        return PolicyIdString(0);
      }
      else if (
        table_name == tpcc_lift_sql::STOCK_TABLE || 
        table_name == tpcc_lift_sql::NEW_ORDER_TABLE ||
        table_name == tpcc_lift_sql::ORDER_TABLE ||
        table_name == tpcc_lift_sql::ORDER_LINE_TABLE ||
        table_name == tpcc_lift_sql::EARLIEST_NEW_ORDER_TABLE ||
        table_name == tpcc_lift_sql::ITEM_TABLE ||
        table_name == tpcc_lift_sql::LATEST_ORDER_TABLE) {
        return PolicyIdString(1);
      }
      else {
        Panic("Unknown table name %s", table_name.c_str());
      }
      // should never trigger
      return "";
    };
  }
  else {
    Panic("Unknown policy function name %s", policy_function_name.c_str());
  }
}

#endif /* _POLICY_FUNCTION_H_ */
