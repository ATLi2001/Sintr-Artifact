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

#include "store/common/sintring/validation_parse_client.h"
#include "store/benchmark/async/tpcc/validation/tpcc_transaction.h"
#include "store/benchmark/async/tpcc/validation/delivery.h"
#include "store/benchmark/async/tpcc/validation/new_order.h"
#include "store/benchmark/async/tpcc/validation/order_status.h"
#include "store/benchmark/async/tpcc/validation/payment.h"
#include "store/benchmark/async/tpcc/validation/stock_level.h"
#include "store/benchmark/async/tpcc/validation/policy_change.h"
#include "store/benchmark/async/tpcc/tpcc-validation-proto.pb.h"
#include "store/benchmark/async/tpcc/tpcc_common.h"
#include "store/benchmark/async/rw-sync/rw-base_transaction.h"
#include "store/benchmark/async/rw-sync/rw-validation-proto.pb.h"
#include "store/benchmark/async/rw-sync/validation/rw-val_transaction.h"
#include "store/benchmark/async/rw-sql/rw-sql-validation-proto.pb.h"
#include "store/benchmark/async/rw-sql/rw-sql_common.h"
#include "store/benchmark/async/rw-sql/validation/rw-sql_val_transaction.h"
#include "store/benchmark/async/rw-sql/validation/rw-sql_val_policy_change.h"
#include "store/benchmark/async/sql/tpcc/tpcc_common.h"
#include "store/benchmark/async/sql/tpcc/validation/delivery.h"
#include "store/benchmark/async/sql/tpcc/validation/new_order.h"
#include "store/benchmark/async/sql/tpcc/validation/order_status.h"
#include "store/benchmark/async/sql/tpcc/validation/payment.h"
#include "store/benchmark/async/sql/tpcc/validation/stock_level.h"
#include "store/benchmark/async/sql/tpcc/validation/policy_change.h"
#include "store/benchmark/async/sql/tpcc/tpcc-sql-validation-proto.pb.h"
#include "store/benchmark/async/smallbank/validation/smallbank_transaction.h"
#include "store/benchmark/async/smallbank/validation/amalgamate.h"
#include "store/benchmark/async/smallbank/validation/bal.h"
#include "store/benchmark/async/smallbank/validation/deposit.h"
#include "store/benchmark/async/smallbank/validation/transact.h"
#include "store/benchmark/async/smallbank/validation/write_check.h"
#include "store/benchmark/async/smallbank/smallbank-validation-proto.pb.h"
#include "store/benchmark/async/smallbank/smallbank_common.h"
#include "store/benchmark/async/sql/seats/seats_common.h"
#include "store/benchmark/async/sql/seats/validation/delete_reservation.h"
#include "store/benchmark/async/sql/seats/validation/find_flights.h"
#include "store/benchmark/async/sql/seats/validation/find_open_seats.h"
#include "store/benchmark/async/sql/seats/validation/new_reservation.h"
#include "store/benchmark/async/sql/seats/validation/update_customer.h"
#include "store/benchmark/async/sql/seats/validation/update_reservation.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"
#include "store/benchmark/async/sql/seats/seats_profile.h"

ValidationTransaction *ValidationParseClient::Parse(const TxnState& txnState) {
  std::string txn_name(txnState.txn_name());
  
  size_t pos = txn_name.find("_");
  if (pos == std::string::npos) {
    Panic("Received unexpected txn name: %s", txn_name.c_str());
  }

  std::string txn_bench = txn_name.substr(0, pos);
  std::string txn_type = txn_name.substr(pos+1);

  if (txn_bench == ::tpcc::BENCHMARK_NAME) {
    ::tpcc::TPCCTransactionType tpcc_txn_type = ::tpcc::GetBenchmarkTxnTypeEnum(txn_type);
    switch (tpcc_txn_type) {
      case ::tpcc::TXN_DELIVERY: {
        ::tpcc::validation::proto::Delivery valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc::ValidationDelivery(timeout, valTxnData);
      }
      case ::tpcc::TXN_NEW_ORDER: {
        ::tpcc::validation::proto::NewOrder valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc::ValidationNewOrder(timeout, valTxnData);
      }
      case ::tpcc::TXN_ORDER_STATUS: {
        ::tpcc::validation::proto::OrderStatus valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc::ValidationOrderStatus(timeout, valTxnData);
      }
      case ::tpcc::TXN_PAYMENT: {
        ::tpcc::validation::proto::Payment valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc::ValidationPayment(timeout, valTxnData);
      }
      case ::tpcc::TXN_STOCK_LEVEL: {
        ::tpcc::validation::proto::StockLevel valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc::ValidationStockLevel(timeout, valTxnData);
      }
      case ::tpcc::TXN_POLICY_CHANGE: {
        ::tpcc::validation::proto::PolicyChange valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc::ValidationPolicyChange(timeout, valTxnData);
      }
      default:
        Panic("Received unexpected txn type: %s", txn_type.c_str());
    }
  }
  else if (txn_bench == ::rwsync::BENCHMARK_NAME) {
    ::rwsync::validation::proto::RWSync valTxnData;
    UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
    return new ::rwsync::RWValTransaction(timeout, keys, valTxnData);
  }
  else if (txn_bench == ::tpcc_sql::BENCHMARK_NAME) {
    ::tpcc_sql::SQLTPCCTransactionType tpcc_txn_type = ::tpcc_sql::GetBenchmarkTxnTypeEnum(txn_type);
    switch (tpcc_txn_type) {
      case ::tpcc_sql::SQL_TXN_DELIVERY: {
        ::tpcc_sql::validation::proto::Delivery valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        UW_ASSERT(!valTxnData.sequential());
        return new ::tpcc_sql::ValidationSQLDelivery(timeout, valTxnData);
      }
      case ::tpcc_sql::SQL_TXN_DELIVERY_SEQUENTIAL: {
        ::tpcc_sql::validation::proto::Delivery valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        UW_ASSERT(valTxnData.sequential());
        return new ::tpcc_sql::ValidationSQLDeliverySequential(timeout, valTxnData);
      }
      case ::tpcc_sql::SQL_TXN_NEW_ORDER: {
        ::tpcc_sql::validation::proto::NewOrder valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        UW_ASSERT(!valTxnData.sequential());
        return new ::tpcc_sql::ValidationSQLNewOrder(timeout, valTxnData);
      }
      case ::tpcc_sql::SQL_TXN_NEW_ORDER_SEQUENTIAL: {
        ::tpcc_sql::validation::proto::NewOrder valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        UW_ASSERT(valTxnData.sequential());
        return new ::tpcc_sql::ValidationSQLNewOrderSequential(timeout, valTxnData);
      }
      case ::tpcc_sql::SQL_TXN_ORDER_STATUS: {
        ::tpcc_sql::validation::proto::OrderStatus valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc_sql::ValidationSQLOrderStatus(timeout, valTxnData);
      }
      case ::tpcc_sql::SQL_TXN_PAYMENT: {
        ::tpcc_sql::validation::proto::Payment valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        UW_ASSERT(!valTxnData.sequential());
        return new ::tpcc_sql::ValidationSQLPayment(timeout, rand, valTxnData);
      }
      case ::tpcc_sql::SQL_TXN_PAYMENT_SEQUENTIAL: {
        ::tpcc_sql::validation::proto::Payment valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        UW_ASSERT(valTxnData.sequential());
        return new ::tpcc_sql::ValidationSQLPaymentSequential(timeout, rand, valTxnData);
      }
      case ::tpcc_sql::SQL_TXN_STOCK_LEVEL: {
        ::tpcc_sql::validation::proto::StockLevel valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc_sql::ValidationSQLStockLevel(timeout, valTxnData);
      }
      case ::tpcc_sql::SQL_TXN_POLICY_CHANGE: {
        ::tpcc_sql::validation::proto::PolicyChange valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::tpcc_sql::ValidationSQLPolicyChange(timeout, valTxnData);
      }
      default:
        Panic("Received unexpected txn type: %s", txn_type.c_str());
    }
  }
  else if (txn_bench == ::rwsql::BENCHMARK_NAME) {
    ::rwsql::RWSQLTransactionType rwsql_txn_type = ::rwsql::GetBenchmarkTxnTypeEnum(txn_type);
    switch (rwsql_txn_type) {
      case ::rwsql::RW_SQL_TRANSACTION: {
        ::rwsql::validation::proto::RWSql valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::rwsql::RWSQLValTransaction(timeout, rand, valTxnData);
      }
      case ::rwsql::RW_SQL_POLICY_CHANGE: {
        ::rwsql::validation::proto::RWSqlPolicyChange valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::rwsql::RWSQLValPolicyChange(timeout, valTxnData);
      }
      default:
        Panic("Received unexpected txn type: %s", txn_type.c_str());
    }
  }
  else if (txn_bench == ::smallbank::BENCHMARK_NAME) {
    ::smallbank::SmallbankTransactionType smallbank_txn_type = ::smallbank::GetBenchmarkTxnTypeEnum(txn_type);
    switch (smallbank_txn_type) {
      case ::smallbank::AMALGAMATE: {
        ::smallbank::validation::proto::Amalgamate valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::smallbank::ValidationAmalgamate(valTxnData, timeout);
      }
      case ::smallbank::BALANCE: {
        ::smallbank::validation::proto::Bal valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::smallbank::ValidationBal(valTxnData, timeout);
      }
      case ::smallbank::DEPOSIT: {
        ::smallbank::validation::proto::Deposit valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::smallbank::ValidationDepositChecking(valTxnData, timeout);
      }
      case ::smallbank::TRANSACT: {
        ::smallbank::validation::proto::Transact valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::smallbank::ValidationTransactSaving(valTxnData, timeout);
      }
      case ::smallbank::WRITE_CHECK: {
        ::smallbank::validation::proto::WriteCheck valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::smallbank::ValidationWriteCheck(valTxnData, timeout);
      }
      default:
        Panic("Received unexpected txn type: %s", txn_type.c_str());
    }
  }
  else if (txn_bench == ::seats_sql::BENCHMARK_NAME) {
    ::seats_sql::SQLSeatsTransactionType seats_sql_txn_type = ::seats_sql::GetBenchmarkTxnTypeEnum(txn_type);
    ::seats_sql::SeatsProfile profile(rand);
    switch (seats_sql_txn_type) {
      case ::seats_sql::SQL_TXN_DELETE_RESERVATION: {
        ::seats_sql::validation::proto::DeleteReservation valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::seats_sql::ValidationSQLDeleteReservation(timeout, rand, profile, valTxnData);
      }
      case ::seats_sql::SQL_TXN_FIND_FLIGHTS: {
        ::seats_sql::validation::proto::FindFlights valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::seats_sql::ValidationSQLFindFlights(timeout, rand, profile, valTxnData);
      }
      case ::seats_sql::SQL_TXN_FIND_OPEN_SEATS: {
        ::seats_sql::validation::proto::FindOpenSeats valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::seats_sql::ValidationSQLFindOpenSeats(timeout, rand, profile, valTxnData);
      }
      case ::seats_sql::SQL_TXN_NEW_RESERVATION: {
        ::seats_sql::validation::proto::NewReservation valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::seats_sql::ValidationSQLNewReservation(timeout, rand, profile, valTxnData);
      }
      case ::seats_sql::SQL_TXN_UPDATE_CUSTOMER: {
        ::seats_sql::validation::proto::UpdateCustomer valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::seats_sql::ValidationSQLUpdateCustomer(timeout, rand, profile, valTxnData);
      }
      case ::seats_sql::SQL_TXN_UPDATE_RESERVATION: {
        ::seats_sql::validation::proto::UpdateReservation valTxnData;
        UW_ASSERT(valTxnData.ParseFromString(txnState.txn_data()));
        return new ::seats_sql::ValidationSQLUpdateReservation(timeout, rand, profile, valTxnData);
      }
      default:
        Panic("Received unexpected txn type: %s", txn_type.c_str());
    }
  }
  else {
    Panic("Received unexpected txn benchmark: %s", txn_bench.c_str());
  }
};
