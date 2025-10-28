#ifndef SYNC_SEATS_SQL_NEW_RESERVATION_H
#define SYNC_SEATS_SQL_NEW_RESERVATION_H 

#include "store/benchmark/async/sql/seats/sync/seats_transaction.h"

#include "store/benchmark/async/sql/seats/new_reservation.h"
#include "store/benchmark/async/sql/seats/seats_profile.h"

#include <queue>

namespace seats_sql {

class SyncSQLNewReservation:public SyncSEATSSQLTransaction, public SQLNewReservation {
    public: 
        SyncSQLNewReservation(uint32_t timeout, std::mt19937 &gen, int64_t r_id, SeatsProfile &profile);
        virtual ~SyncSQLNewReservation();
        virtual transaction_status_t Execute(SyncClient &client);
};

}

#endif

