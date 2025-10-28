#ifndef SYNC_SEATS_SQL_DELETE_RESERVATION_H
#define SYNC_SEATS_SQL_DELETE_RESERVATION_H 

#include "store/benchmark/async/sql/seats/sync/seats_transaction.h"

#include <random>
#include <queue>

#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/delete_reservation.h"

namespace seats_sql {

class SyncSQLDeleteReservation: public SyncSEATSSQLTransaction, public SQLDeleteReservation {
    public: 
        SyncSQLDeleteReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile);
        virtual ~SyncSQLDeleteReservation();
        virtual transaction_status_t Execute(SyncClient &client);
};

}
#endif

