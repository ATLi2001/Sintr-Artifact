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
#include "store/benchmark/async/tpcc/stock_level.h"

#include <chrono>
#include <sstream>
#include <ctime>

#include "store/benchmark/async/tpcc/tpcc_utils.h"
#include "store/benchmark/async/tpcc/tpcc_common.h"
#include "store/benchmark/async/tpcc/tpcc-validation-proto.pb.h"
#include "store/common/common-proto.pb.h"


namespace tpcc {

StockLevel::StockLevel(uint32_t w_id, uint32_t d_id, std::mt19937 &gen) :
    w_id(w_id), d_id(d_id) {
  min_quantity = std::uniform_int_distribution<uint8_t>(10, 20)(gen);
}

StockLevel::~StockLevel() {
}

void StockLevel::SerializeTxnState(std::string &txnState) {
  TxnState currTxnState = TxnState();
  std::string txn_name;
  txn_name.append(BENCHMARK_NAME);
  txn_name.push_back('_');
  txn_name.append(GetBenchmarkTxnTypeName(TXN_STOCK_LEVEL));
  currTxnState.set_txn_name(txn_name);

  validation::proto::StockLevel curr_txn = validation::proto::StockLevel();
  curr_txn.set_w_id(w_id);
  curr_txn.set_d_id(d_id);
  curr_txn.set_min_quantity(min_quantity);
  std::vector<Tables> est_tables = StockLevel::HeuristicFunction();
  for(const auto& value : est_tables) {
    curr_txn.add_est_tables((int)value);
  }

  std::string txn_data;
  curr_txn.SerializeToString(&txn_data);
  currTxnState.set_txn_data(txn_data);

  currTxnState.SerializeToString(&txnState);
}

transaction_status_t StockLevel::BaseExecute(SyncClient &client, int timeout, bool serialize) {
  std::string str;
  std::vector<std::string> strs;

  Debug("STOCK_LEVEL");
  Debug("Warehouse: %u", w_id);
  Debug("District: %u", d_id);
  //std::cerr << "warehouse: " << w_id << std::endl;

  std::string txnState;
  if(serialize) {
    StockLevel::SerializeTxnState(txnState);
  }

  client.Begin(timeout, txnState);

  std::string d_key = DistrictRowKey(w_id, d_id);
  client.Get(d_key, str, timeout);
  DistrictRow d_row;
  UW_ASSERT(d_row.ParseFromString(str));

  uint32_t next_o_id = d_row.next_o_id();
  Debug("Orders: %u-%u", next_o_id - 20, next_o_id - 1);

  for (size_t ol_o_id = next_o_id - 20; ol_o_id < next_o_id; ++ol_o_id) {
    Debug("Order %lu", ol_o_id);
    client.Get(OrderRowKey(w_id, d_id, ol_o_id), timeout);
  }

  client.Wait(strs);

  std::map<uint32_t, uint32_t> ol_cnts;
  for (uint32_t ol_o_id = next_o_id - 20; ol_o_id < next_o_id; ++ol_o_id) {
    if (strs[ol_o_id + 20 - next_o_id].empty()) {
      Debug("  Non-existent Order %u", ol_o_id);
      continue;
    }
    OrderRow o_row;
    UW_ASSERT(o_row.ParseFromString(strs[ol_o_id + 20 - next_o_id]));
    Debug("  Order Lines: %u", o_row.ol_cnt());

    ol_cnts[ol_o_id] = o_row.ol_cnt();
    for (size_t ol_number = 0; ol_number < o_row.ol_cnt(); ++ol_number) {
      Debug("    OL %lu", ol_number);
      std::string ol_key = OrderLineRowKey(w_id, d_id, ol_o_id, ol_number);
      client.Get(ol_key, timeout);
    }
  }

  strs.clear();
  client.Wait(strs);

  std::map<uint32_t, StockRow> stockRows;
  size_t strsIdx = 0;
  for (const auto order_cnt : ol_cnts) {
    Debug("Order %u", order_cnt.first);
    for (size_t ol_number = 0; ol_number < order_cnt.second; ++ol_number) {
      OrderLineRow ol_row;
      Debug("  OL %lu", ol_number);
      Debug("  Total OL index: %lu.", strsIdx);
      if (strs[strsIdx].empty()) {
        Debug("  Non-existent Order Line %lu", ol_number);
        continue;
      }
      UW_ASSERT(ol_row.ParseFromString(strs[strsIdx]));
      strsIdx++;
      Debug("      Item %d", ol_row.i_id());

      if (stockRows.find(ol_row.i_id()) == stockRows.end()) {
        client.Get(StockRowKey(w_id, ol_row.i_id()), timeout);
      }
    }
  }

  strs.clear();
  client.Wait(strs);

  Debug("COMMIT");
  return client.Commit(timeout);
}

std::vector<Tables> StockLevel::HeuristicFunction() {
  return {DISTRICT, ORDER_LINE, STOCK};
}

}
