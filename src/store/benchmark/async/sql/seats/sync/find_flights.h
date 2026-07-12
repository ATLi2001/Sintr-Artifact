#ifndef SYNC_SEATS_SQL_FIND_FLIGHTS_H
#define SYNC_SEATS_SQL_FIND_FLIGHTS_H 

#include "store/benchmark/async/sql/seats/sync/seats_transaction.h"
#include <random>

#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/find_flights.h"

namespace seats_sql {

class SyncSQLFindFlights: public SyncSEATSSQLTransaction, public SQLFindFlights {
    public: 
        SyncSQLFindFlights(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile); 
        virtual ~SyncSQLFindFlights();
        virtual transaction_status_t Execute(SyncClient &client);
};

}
#endif

