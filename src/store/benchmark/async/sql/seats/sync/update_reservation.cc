#include "store/benchmark/async/sql/seats/sync/update_reservation.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"

#include <fmt/core.h>

namespace seats_sql {
SyncSQLUpdateReservation::SyncSQLUpdateReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile) : 
    SQLUpdateReservation(timeout, gen, profile), SyncSEATSSQLTransaction(timeout) {
    }
SyncSQLUpdateReservation::~SyncSQLUpdateReservation() {}


transaction_status_t SyncSQLUpdateReservation::Execute(SyncClient &client) {
    return SQLUpdateReservation::BaseExecute(client, true);
}

}

