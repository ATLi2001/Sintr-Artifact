#include "store/benchmark/async/sql/seats/validation/delete_reservation.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"

#include <fmt/core.h>
#include <random> 

namespace seats_sql{

ValidationSQLDeleteReservation::ValidationSQLDeleteReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::DeleteReservation &deleteReservationMsg) : 
    SQLDeleteReservation(timeout, gen, profile, deleteReservationMsg), ValidationSEATSSQLTransaction(timeout) {
    }
ValidationSQLDeleteReservation::~ValidationSQLDeleteReservation() {}


transaction_status_t ValidationSQLDeleteReservation::Validate(SyncClient &client) {
    return SQLDeleteReservation::BaseExecute(client, false);
}
}
