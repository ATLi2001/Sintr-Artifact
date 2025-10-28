/***********************************************************************
 *
 * Copyright 2024 Daniel Lee <dhl93@cornell.edu>
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
#ifndef SEATS_COMMON_H
#define SEATS_COMMON_H

#include "lib/message.h"
#include <string>

namespace seats_sql {

enum SQLSeatsTransactionType {
  SQL_TXN_DELETE_RESERVATION = 0,
  SQL_TXN_FIND_FLIGHTS,
  SQL_TXN_FIND_OPEN_SEATS,
  SQL_TXN_NEW_RESERVATION,
  SQL_TXN_UPDATE_CUSTOMER,
  SQL_TXN_UPDATE_RESERVATION
};

const std::string BENCHMARK_NAME = "seats-sql";

inline std::string GetBenchmarkTxnTypeName(SQLSeatsTransactionType txn_type) {
  switch (txn_type) {
    case SQL_TXN_DELETE_RESERVATION:
      return "delete_reservation";
    case SQL_TXN_FIND_FLIGHTS:
      return "find_flights";
    case SQL_TXN_FIND_OPEN_SEATS:
      return "find_open_seats";
    case SQL_TXN_NEW_RESERVATION:
      return "new_reservation";
    case SQL_TXN_UPDATE_CUSTOMER:
      return "update_customer";
    case SQL_TXN_UPDATE_RESERVATION:
      return "update_reservation";
    default:
      Panic("Received unexpected txn type: %d", txn_type);
  }
}

inline SQLSeatsTransactionType GetBenchmarkTxnTypeEnum(std::string &txn_type) {
  if (txn_type == "delete_reservation") {
    return SQL_TXN_DELETE_RESERVATION;
  }
  else if (txn_type == "find_flights") {
    return SQL_TXN_FIND_FLIGHTS;
  }
  else if (txn_type == "find_open_seats") {
    return SQL_TXN_FIND_OPEN_SEATS;
  }
  else if (txn_type == "new_reservation") {
    return SQL_TXN_NEW_RESERVATION;
  }
  else if (txn_type == "update_customer") {
    return SQL_TXN_UPDATE_CUSTOMER;
  }
  else if (txn_type == "update_reservation") {
    return SQL_TXN_UPDATE_RESERVATION;
  }
  else {
    Panic("Received unexpected txn type: %s", txn_type.c_str());
  }
}

}

#endif /* SEATS_COMMON_H */
