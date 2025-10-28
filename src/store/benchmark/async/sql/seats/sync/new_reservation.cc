#include "store/benchmark/async/sql/seats/sync/new_reservation.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"

#include <fmt/core.h>
#include <queue>

namespace seats_sql {
    
SyncSQLNewReservation::SyncSQLNewReservation(uint32_t timeout, std::mt19937 &gen, int64_t r_id, SeatsProfile &profile) : 
   SQLNewReservation(timeout, gen, r_id, profile), SyncSEATSSQLTransaction(timeout)
{
}

SyncSQLNewReservation::~SyncSQLNewReservation() {} 

transaction_status_t SyncSQLNewReservation::Execute(SyncClient &client) {
    return SQLNewReservation::BaseExecute(client, true);
}

}