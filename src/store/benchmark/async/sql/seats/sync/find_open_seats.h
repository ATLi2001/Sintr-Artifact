#ifndef SYNC_SEATS_SQL_FIND_OPEN_SEATS_H
#define SYNC_SEATS_SQL_FIND_OPEN_SEATS_H 

#include "store/benchmark/async/sql/seats/sync/seats_transaction.h"

#include <random>
#include <queue>
#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/find_open_seats.h"

namespace seats_sql {

class SyncSQLFindOpenSeats: public SyncSEATSSQLTransaction, public SQLFindOpenSeats {
    public: 
        SyncSQLFindOpenSeats(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile);
        virtual ~SyncSQLFindOpenSeats();
        virtual transaction_status_t Execute(SyncClient &client);
};

}

#endif

