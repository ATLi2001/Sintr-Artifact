#include "store/benchmark/async/sql/seats/sync/find_flights.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"
#include <fmt/core.h>
#include <random>

namespace seats_sql {

SyncSQLFindFlights::SyncSQLFindFlights(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile) :
    SQLFindFlights(timeout, gen, profile), SyncSEATSSQLTransaction(timeout)
    { }

SyncSQLFindFlights::~SyncSQLFindFlights() {}

transaction_status_t SyncSQLFindFlights::Execute(SyncClient &client) {
    return SQLFindFlights::BaseExecute(client, true);
}
}
