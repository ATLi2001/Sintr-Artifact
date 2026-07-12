#include "store/benchmark/async/sql/seats/sync/delete_reservation.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"
#include <fmt/core.h>
#include <random> 

namespace seats_sql{

SyncSQLDeleteReservation::SyncSQLDeleteReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile) : 
    SQLDeleteReservation(timeout, gen, profile), SyncSEATSSQLTransaction(timeout) {
    }
SyncSQLDeleteReservation::~SyncSQLDeleteReservation() {}


transaction_status_t SyncSQLDeleteReservation::Execute(SyncClient &client) {
    return SQLDeleteReservation::BaseExecute(client, true);
}
}
