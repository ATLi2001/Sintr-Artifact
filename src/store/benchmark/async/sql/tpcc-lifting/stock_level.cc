/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Matthew Burke <matthelb@cs.cornell.edu>
 *                Liam Arzola <lma77@cornell.edu>
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
#include "store/benchmark/async/sql/tpcc-lifting/stock_level.h"

#include <map>
#include <fmt/core.h>

#include "store/benchmark/async/sql/tpcc-lifting/tpcc_common.h"
#include "store/benchmark/async/sql/tpcc-lifting/tpcc-lift-sql-validation-proto.pb.h"
#include "store/common/common-proto.pb.h"
#include "store/benchmark/async/sql/tpcc-lifting/tpcc_utils.h"

namespace tpcc_lift_sql {

SQLStockLevel::SQLStockLevel(uint32_t w_id, uint32_t d_id,
    std::mt19937 &gen) : w_id(w_id), d_id(d_id) {
  min_quantity = std::uniform_int_distribution<uint8_t>(10, 20)(gen);

  std::cerr << "STOCK_LEVEL (parallel)" << std::endl;
}

SQLStockLevel::~SQLStockLevel() {
}

transaction_status_t SQLStockLevel::BaseExecute(SyncClient &client, uint32_t timeout, bool serialize) {
  std::unique_ptr<const query_result::QueryResult> queryResult;
  std::string query;
  std::vector<std::unique_ptr<const query_result::QueryResult>> results;

  //Determine the number of recently sold items with stock below a given threshold
  //Type: Heavy read-only Tx, low frequency
  Debug("STOCK_LEVEL (parallel)");
  Debug("Warehouse: %u", w_id);
  Debug("District: %u", d_id);
  //std::cerr << "warehouse: " << w_id << std::endl;

  std::string txnState;
  if (serialize) {
    SQLStockLevel::SerializeTxnState(txnState);
  }

  client.Begin(timeout, txnState);

  // (1) Select the specified row from District and extract the Next Order Id
  query = fmt::format("SELECT lo_o_id FROM {} WHERE lo_d_id = {} AND lo_w_id = {}", LATEST_ORDER_TABLE, d_id, w_id);
  client.Query(query, queryResult, timeout);
  uint32_t next_o_id;
  deserialize(next_o_id, queryResult, 0, 0); // point read
  Debug("Orders: %u-%u", next_o_id - 20, next_o_id - 1);


  if(join_free_version){
      query = fmt::format("SELECT ol_i_id FROM {} WHERE ol_w_id = {} AND ol_d_id = {} AND ol_o_id < {} AND ol_o_id >= {} ", ORDER_LINE_TABLE, w_id, d_id, next_o_id, next_o_id - 20);
      client.Query(query, queryResult, timeout);  

      //For each unique item.
      std::set<uint32_t> stockItems;
      size_t strsIdx = 0;
      for (int i = 0; i < queryResult->size(); ++i) {
        uint32_t i_id;
        deserialize(i_id, queryResult, i);
        Debug("Item %d", i_id);

        //Check all distinct items.
        if (stockItems.insert(i_id).second) {  //ca 200 point reads...
            query = fmt::format("SELECT s_i_id FROM {} WHERE s_w_id = {} AND s_i_id = {} AND s_quantity < {}", STOCK_TABLE, w_id, i_id, min_quantity);
            client.Query(query, timeout);  
          } 
      }
      client.Wait(results);
      // Num distinct stocks with quant < min_quant == Number of results that are non-empty.
  }
  else{
      // // (2) Select the 20 most recent orders from the district: Select the orders from OrderLine (from this district) with    next_o_id - 20 <= id < next_o_id
      // // (3) Count all rows in STOCK with distinct items whose quantity is below the min_quantity threshold.
      query = fmt::format("SELECT COUNT(DISTINCT(s_i_id)) FROM {}, {} "
                          "WHERE ol_w_id = {} AND ol_d_id = {} AND ol_o_id < {} AND ol_o_id >= {} "
                          " AND s_i_id = ol_i_id AND s_w_id = {} AND s_quantity < {} "
                          "AND s_i_id = s_i_id", //REFLEXIVE TO TRICK PELOTONS DUMB JOIN PLANNER 
                          ORDER_LINE_TABLE, STOCK_TABLE, w_id, d_id, next_o_id, next_o_id - 20, w_id, min_quantity);

      // query = fmt::format("SELECT COUNT(DISTINCT(s_i_id)) FROM {} INNER JOIN {} ON s_i_id = ol_i_id "
      //                   "WHERE ol_w_id = {} AND ol_d_id = {} AND ol_o_id < {} AND ol_o_id >= {} AND s_w_id = {} AND s_quantity < {}",
      //                   ORDER_LINE_TABLE, STOCK_TABLE, w_id, d_id, next_o_id, next_o_id - 20, w_id, min_quantity);
      //TODO: Write it as a an explicit JOIN somehow to more easily extract individual table predicates?
      // query = fmt::format("SELECT COUNT(DISTINCT(Stock.i_id)) FROM (SELECT * FROM OrderLine WHERE w_id = {} AND d_id = {} AND o_id < {} AND o_id >= {}) "
      //                     "LEFT JOIN (SELECT * FROM STOCK WHERE w_id = {} AND quantity < {}) " //This is super inefficient..
      //                     "ON Stock.i_id = OrderLine.i_id;", w_id, d_id, next_o_id, next_o_id - 20, w_id, min_quantity);

      client.Query(query, queryResult, timeout);
      uint32_t stock_count;
      deserialize(stock_count, queryResult);
      Debug("Stock Count: %u", stock_count);
  }

  Debug("COMMIT");
  return client.Commit(timeout);
}

void SQLStockLevel::SerializeTxnState(std::string &txnState) {
  TxnState currTxnState = TxnState();
  std::string txn_name;
  txn_name.append(BENCHMARK_NAME);
  txn_name.push_back('_');
  txn_name.append(GetBenchmarkTxnTypeName(SQL_TXN_STOCK_LEVEL));
  currTxnState.set_txn_name(txn_name);

  validation::proto::StockLevel curr_txn = validation::proto::StockLevel();
  curr_txn.set_w_id(w_id);
  curr_txn.set_d_id(d_id);
  curr_txn.set_min_quantity(min_quantity);
  std::vector<TPCC_Table> est_tables = SQLStockLevel::HeuristicFunction();
  for(const auto& value : est_tables) {
    curr_txn.add_est_tables((int)value);
  }

  std::string txn_data;
  curr_txn.SerializeToString(&txn_data);
  currTxnState.set_txn_data(txn_data);

  currTxnState.SerializeToString(&txnState);
}

std::vector<TPCC_Table> SQLStockLevel::HeuristicFunction() {
  return {LATEST_ORDER, ORDER_LINE, STOCK};
}

} // namespace tpcc_lift_sql
