#include "store/benchmark/async/sql/seats/validation/update_customer.h"
#include "store/benchmark/async/sql/seats/seats_constants.h"
#include <fmt/core.h>

namespace seats_sql {
ValidationSQLUpdateCustomer::ValidationSQLUpdateCustomer(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::UpdateCustomer &updateCustomerMsg) : 
    SQLUpdateCustomer(timeout, gen, profile, updateCustomerMsg), ValidationSEATSSQLTransaction(timeout) {
    }
ValidationSQLUpdateCustomer::~ValidationSQLUpdateCustomer() {}


transaction_status_t ValidationSQLUpdateCustomer::Validate(SyncClient &client) {
    return SQLUpdateCustomer::BaseExecute(client, false);
}
}
