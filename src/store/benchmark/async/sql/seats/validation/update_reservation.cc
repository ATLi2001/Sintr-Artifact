#include "store/benchmark/async/sql/seats/validation/update_reservation.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"

#include <fmt/core.h>

namespace seats_sql {
ValidationSQLUpdateReservation::ValidationSQLUpdateReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile,
    const validation::proto::UpdateReservation &updateReservationMsg) : 
    SQLUpdateReservation(timeout, gen, profile, updateReservationMsg), ValidationSEATSSQLTransaction(timeout) {
    }
ValidationSQLUpdateReservation::~ValidationSQLUpdateReservation() {}


transaction_status_t ValidationSQLUpdateReservation::Validate(SyncClient &client) {
    return SQLUpdateReservation::BaseExecute(client, false);
}

}

