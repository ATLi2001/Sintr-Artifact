#ifndef VALIDATION_SEATS_SQL_FIND_FLIGHTS_H
#define VALIDATION_SEATS_SQL_FIND_FLIGHTS_H 

#include "store/benchmark/async/sql/seats/validation/seats_transaction.h"
#include "store/benchmark/async/sql/seats/seats-sql-validation-proto.pb.h"

#include <random>

#include "store/benchmark/async/sql/seats/seats_profile.h"
#include "store/benchmark/async/sql/seats/find_flights.h"

namespace seats_sql {

class ValidationSQLFindFlights: public ValidationSEATSSQLTransaction, public SQLFindFlights {
    public: 
        ValidationSQLFindFlights(uint32_t timeout, std::mt19937 &gen, SeatsProfile &profile, const validation::proto::FindFlights &findFlightsMsg); 
        virtual ~ValidationSQLFindFlights();
        transaction_status_t Validate(SyncClient &client);
};

}
#endif

