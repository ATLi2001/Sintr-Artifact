#ifndef SYNC_SEATS_SQL_UPDATE_CUSTOMER_H
#define SYNC_SEATS_SQL_UPDATE_CUSTOMER_H 

#include "store/benchmark/async/sql/seats/sync/seats_transaction.h"
#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/update_customer.h"
#include "store/benchmark/async/sql/seats/seats_common.h"

namespace seats_sql {

class SyncSQLUpdateCustomer:public SyncSEATSSQLTransaction, public SQLUpdateCustomer {
    public: 
        SyncSQLUpdateCustomer(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile);
        virtual ~SyncSQLUpdateCustomer();
        virtual transaction_status_t Execute(SyncClient &client);
};

}

#endif

