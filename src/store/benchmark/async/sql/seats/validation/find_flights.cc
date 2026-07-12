#include "store/benchmark/async/sql/seats/validation/find_flights.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"

#include <fmt/core.h>
#include <random>

namespace seats_sql {

ValidationSQLFindFlights::ValidationSQLFindFlights(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::FindFlights &findFlightsMsg) :
    SQLFindFlights(timeout, gen, profile, findFlightsMsg), ValidationSEATSSQLTransaction(timeout)
    { }

ValidationSQLFindFlights::~ValidationSQLFindFlights() {}

transaction_status_t ValidationSQLFindFlights::Validate(SyncClient &client) {
    return SQLFindFlights::BaseExecute(client, false);
}
}
