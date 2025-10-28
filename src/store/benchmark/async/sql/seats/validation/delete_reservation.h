#ifndef VALIDATION_SEATS_SQL_DELETE_RESERVATION_H
#define VALIDATION_SEATS_SQL_DELETE_RESERVATION_H 

#include "store/benchmark/async/sql/seats/validation/seats_transaction.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"

#include <random>
#include <queue>

#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/delete_reservation.h"

namespace seats_sql {

class ValidationSQLDeleteReservation: public ValidationSEATSSQLTransaction, public SQLDeleteReservation {
    public: 
        ValidationSQLDeleteReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::DeleteReservation &deleteReservationMsg);
        virtual ~ValidationSQLDeleteReservation();
        transaction_status_t Validate(SyncClient &client);
};

}
#endif

