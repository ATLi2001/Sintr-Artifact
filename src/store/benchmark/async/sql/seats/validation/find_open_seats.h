#ifndef VALIDATION_SEATS_SQL_FIND_OPEN_SEATS_H
#define VALIDATION_SEATS_SQL_FIND_OPEN_SEATS_H 

#include "store/benchmark/async/sql/seats/validation/seats_transaction.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"

#include <random>
#include <queue>
#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/find_open_seats.h"

namespace seats_sql {

class ValidationSQLFindOpenSeats: public ValidationSEATSSQLTransaction, public SQLFindOpenSeats {
    public: 
        ValidationSQLFindOpenSeats(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::FindOpenSeats &findOpenSeatsMsg);
        virtual ~ValidationSQLFindOpenSeats();
        transaction_status_t Validate(SyncClient &client);
};

}

#endif

