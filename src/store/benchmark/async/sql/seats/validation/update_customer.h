#ifndef VALIDATION_SEATS_SQL_UPDATE_CUSTOMER_H
#define VALIDATION_SEATS_SQL_UPDATE_CUSTOMER_H 

#include "store/benchmark/async/sql/seats/validation/seats_transaction.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"

#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/update_customer.h"
#include "store/benchmark/async/sql/seats/seats_common.h"

namespace seats_sql {

class ValidationSQLUpdateCustomer:public ValidationSEATSSQLTransaction, public SQLUpdateCustomer {
    public: 
        ValidationSQLUpdateCustomer(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::UpdateCustomer &updateCustomerMsg);
        virtual ~ValidationSQLUpdateCustomer();
        transaction_status_t Validate(SyncClient &client);
};

}

#endif

