/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
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
#include "store/benchmark/async/sql/auctionmark/transactions/new_feedback.h"
#include "store/benchmark/async/sql/auctionmark/auctionmark_common.h"
#include "store/benchmark/async/sql/auctionmark/auctionmark-validation-proto.pb.h"
#include "store/common/common-proto.pb.h"
#include <fmt/core.h>

namespace auctionmark {

NewFeedback::NewFeedback(uint32_t timeout, AuctionMarkProfile &profile, std::mt19937_64 &gen) : AuctionMarkTransaction(timeout), profile(profile), gen(gen) {
  
  std::cerr << std::endl << "NEW FEEDBACK" << std::endl;
  std::optional<ItemInfo> maybeItemInfo = profile.get_random_completed_item();
  ItemInfo itemInfo;
  if (maybeItemInfo.has_value()) {
    itemInfo = maybeItemInfo.value();
  } else {
    throw std::runtime_error("new_feedback construction: failed to get random completed item");
  }
  UserId sellerId = itemInfo.get_seller_id();
  UserId buyerId = profile.get_random_buyer_id(sellerId);
  rating = std::uniform_int_distribution<int>(-1, 1)(gen);
  feedback = RandomAString(10, 80, gen);

  if(std::uniform_int_distribution<int>(0, 1)(gen)){
    user_id = sellerId.encode();
    from_id = buyerId.encode();
  } else {
    user_id = buyerId.encode();
    from_id = sellerId.encode();
  }

  i_id = itemInfo.get_item_id().encode();
  seller_id = sellerId.encode();
}

NewFeedback::~NewFeedback(){
}

transaction_status_t NewFeedback::BaseExecute(SyncClient &client, bool serialize) {
  std::unique_ptr<const query_result::QueryResult> queryResult;
  std::string statement;
  std::vector<std::unique_ptr<const query_result::QueryResult>> results;

  Debug("NEW FEEDBACK");

  uint64_t current_time = GetProcTimestamp({profile.get_loader_start_time(), profile.get_client_start_time()});

  std::string txnState;
  if(serialize) {
    SerializeTxnState(txnState);
  }
  client.Begin(timeout, txnState);

  //checkUserFeedback
  statement = fmt::format("SELECT uf_i_id, uf_i_u_id, uf_from_id FROM {} WHERE uf_u_id = '{}' AND uf_i_id = '{}' AND uf_i_u_id = '{}' AND uf_from_id = '{}'", 
                                                                  TABLE_USERACCT_FEEDBACK, user_id, i_id, seller_id, from_id);
  client.Query(statement, queryResult, timeout);
  if(!queryResult->empty()){
    Debug("Trying to add feedback for item %s twice", i_id.c_str());
    client.Abort(timeout);
    return ABORTED_USER;
  }

  std::string insertFeedback = fmt::format("INSERT INTO {} (uf_u_id, uf_i_id, uf_i_u_id, uf_from_id, uf_rating, uf_date, uf_sattr0) "
                          "VALUES ('{}', '{}', '{}', '{}', {}, {}, '{}')", TABLE_USERACCT_FEEDBACK, user_id, i_id, seller_id, from_id, rating, current_time, feedback);
  client.Write(insertFeedback, timeout, true, true); //async, blind-write

  std::string updateUser = fmt::format("UPDATE {} SET u_rating = u_rating + {} WHERE u_id = '{}'", TABLE_USERACCT, rating, current_time, user_id);
  client.Write(updateUser, timeout, true);

  client.asyncWait();

  Debug("COMMIT NEW_FEEDBACK");
  return client.Commit(timeout);
}

void NewFeedback::SerializeTxnState(std::string &txnState) {
  TxnState currTxnState;
  std::string txn_name;
  txn_name.append(BENCHMARK_NAME);
  txn_name.push_back('_');
  txn_name.append(GetBenchmarkTxnTypeName(TXN_NEW_FEEDBACK));
  currTxnState.set_txn_name(txn_name);

  validation::proto::NewFeedback curr_txn;
  curr_txn.set_user_id(user_id);
  curr_txn.set_i_id(i_id);
  curr_txn.set_seller_id(seller_id);
  curr_txn.set_from_id(from_id);
  curr_txn.set_rating(rating);
  curr_txn.set_feedback(feedback);

  curr_txn.SerializeToString(currTxnState.mutable_txn_data());
  currTxnState.SerializeToString(&txnState);
}


} // namespace auctionmark
