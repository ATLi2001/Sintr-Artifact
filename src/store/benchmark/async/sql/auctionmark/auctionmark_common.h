/***********************************************************************
 *
 * Copyright 2025 Austin Li <atl63@cornell.edu>
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

#ifndef AUCTIONMARK_COMMON_H
#define AUCTIONMARK_COMMON_H

#include "store/benchmark/async/sql/auctionmark/auctionmark_client.h"
#include "lib/message.h"
#include <string>

namespace auctionmark {

const std::string BENCHMARK_NAME = "auctionmark";

inline std::string GetBenchmarkTxnTypeName(AuctionMarkTransactionType txn_type) {
  switch (txn_type) {
    case TXN_NEW_USER:
      return "new_user";
    case TXN_NEW_ITEM:
      return "new_item";
    case TXN_NEW_BID:
      return "new_bid";
    case TXN_NEW_COMMENT:
      return "new_comment";
    case TXN_NEW_COMMENT_RESPONSE:
      return "new_comment_response";
    case TXN_NEW_PURCHASE:
      return "new_purchase";
    case TXN_NEW_FEEDBACK:
      return "new_feedback";
    case TXN_GET_ITEM:
      return "get_item";
    case TXN_UPDATE_ITEM:
      return "update_item";
    case TXN_GET_COMMENT:
      return "get_comment";
    case TXN_GET_USER_INFO:
      return "get_user_info";
    case TXN_GET_WATCHED_ITEM:
      return "get_watched_item";
    default:
      Panic("Received unexpected txn type: %d", txn_type);
  }
}

inline AuctionMarkTransactionType GetBenchmarkTxnTypeEnum(std::string &txn_type) {
  if (txn_type == "new_user") {
    return TXN_NEW_USER;
  }
  else if (txn_type == "new_item") {
    return TXN_NEW_ITEM;
  }
  else if (txn_type == "new_bid") {
    return TXN_NEW_BID;
  }
  else if (txn_type == "new_comment") {
    return TXN_NEW_COMMENT;
  }
  else if (txn_type == "new_comment_response") {
    return TXN_NEW_COMMENT_RESPONSE;
  }
  else if (txn_type == "new_purchase") {
    return TXN_NEW_PURCHASE;
  }
  else if (txn_type == "new_feedback") {
    return TXN_NEW_FEEDBACK;
  }
  else if (txn_type == "get_item") {
    return TXN_GET_ITEM;
  }
  else if (txn_type == "update_item") {
    return TXN_UPDATE_ITEM;
  }
  else if (txn_type == "get_comment") {
    return TXN_GET_COMMENT;
  }
  else if (txn_type == "get_user_info") {
    return TXN_GET_USER_INFO;
  }
  else if (txn_type == "get_watched_item") {
    return TXN_GET_WATCHED_ITEM;
  }
  else {
    Panic("Received unexpected txn type: %s", txn_type.c_str());
  }
}

}

#endif /* AUCTIONMARK_COMMON_H */
