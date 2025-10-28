#include "store/benchmark/async/sql/seats/sync/find_open_seats.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats_common.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"

#include <fmt/core.h>
#include <queue>


namespace seats_sql {

SyncSQLFindOpenSeats::SyncSQLFindOpenSeats(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile) : 
    SQLFindOpenSeats(timeout, gen, profile), SyncSEATSSQLTransaction(timeout) {
    }

SyncSQLFindOpenSeats::~SyncSQLFindOpenSeats() {};

transaction_status_t SyncSQLFindOpenSeats::Execute(SyncClient &client) {
    return SQLFindOpenSeats::BaseExecute(client, true);
}

}