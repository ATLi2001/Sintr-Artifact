#ifndef SYNC_SEATS_SQL_UPDATE_RESERVATION_H
#define SYNC_SEATS_SQL_UPDATE_RESERVATION_H

#include "store/benchmark/async/sql/seats/sync/seats_transaction.h"

#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/update_reservation.h"
#include <queue>

namespace seats_sql {

class SyncSQLUpdateReservation:public SyncSEATSSQLTransaction, public SQLUpdateReservation {
    public: 
        SyncSQLUpdateReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile);
        virtual ~SyncSQLUpdateReservation();
        virtual transaction_status_t Execute(SyncClient &client);
};
}

#endif

