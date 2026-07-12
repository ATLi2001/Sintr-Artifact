#include "store/benchmark/async/sql/seats/validation/new_reservation.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"

#include <fmt/core.h>
#include <queue>

namespace seats_sql {
    
ValidationSQLNewReservation::ValidationSQLNewReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::NewReservation &newReservationMsg) : 
   SQLNewReservation(timeout, gen, profile, newReservationMsg), ValidationSEATSSQLTransaction(timeout)
{
}

ValidationSQLNewReservation::~ValidationSQLNewReservation() {} 

transaction_status_t ValidationSQLNewReservation::Validate(SyncClient &client) {
    return SQLNewReservation::BaseExecute(client, false);
}

}