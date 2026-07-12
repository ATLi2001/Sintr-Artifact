#ifndef VALIDATION_SEATS_SQL_UPDATE_RESERVATION_H
#define VALIDATION_SEATS_SQL_UPDATE_RESERVATION_H

#include "store/benchmark/async/sql/seats/validation/seats_transaction.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"

#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/update_reservation.h"
#include <queue>

namespace seats_sql {

class ValidationSQLUpdateReservation:public ValidationSEATSSQLTransaction, public SQLUpdateReservation {
    public: 
        ValidationSQLUpdateReservation(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::UpdateReservation &updateReservationMsg);
        virtual ~ValidationSQLUpdateReservation();
        transaction_status_t Validate(SyncClient &client);
};
}

#endif

