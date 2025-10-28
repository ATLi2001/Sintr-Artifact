#include "store/benchmark/async/sql/seats/sync/update_customer.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"
#include <fmt/core.h>

namespace seats_sql {
SyncSQLUpdateCustomer::SyncSQLUpdateCustomer(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile) : 
    SQLUpdateCustomer(timeout, gen, profile), SyncSEATSSQLTransaction(timeout) {
    }
SyncSQLUpdateCustomer::~SyncSQLUpdateCustomer() {}


transaction_status_t SyncSQLUpdateCustomer::Execute(SyncClient &client) {
    return SQLUpdateCustomer::BaseExecute(client, true);
}
}
