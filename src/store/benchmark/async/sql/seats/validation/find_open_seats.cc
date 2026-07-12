#include "store/benchmark/async/sql/seats/validation/find_open_seats.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"

#include <fmt/core.h>
#include <queue>


namespace seats_sql {

ValidationSQLFindOpenSeats::ValidationSQLFindOpenSeats(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::FindOpenSeats &findOpenSeatsMsg) : 
    SQLFindOpenSeats(timeout, gen, profile, findOpenSeatsMsg), ValidationSEATSSQLTransaction(timeout) {
    }

ValidationSQLFindOpenSeats::~ValidationSQLFindOpenSeats() {};

transaction_status_t ValidationSQLFindOpenSeats::Validate(SyncClient &client) {
    return SQLFindOpenSeats::BaseExecute(client, false);
}

}