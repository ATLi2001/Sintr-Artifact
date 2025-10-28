#ifndef VALIDATION_SEATS_SQL_NEW_RESERVATION_H
#define VALIDATION_SEATS_SQL_NEW_RESERVATION_H 

#include "store/benchmark/async/sql/seats/validation/seats_transaction.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"

#include "store/benchmark/async/sql/seats/new_reservation.h"
#include "store/benchmark/async/sql/seats/seats_profile.h"

#include <queue>

namespace seats_sql {

class ValidationSQLNewReservation:public ValidationSEATSSQLTransaction, public SQLNewReservation {
    public: 
        ValidationSQLNewReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::NewReservation &newReservationMsg);
        virtual ~ValidationSQLNewReservation();
        transaction_status_t Validate(SyncClient &client);
};

}

#endif

