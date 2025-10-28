#ifndef VALIDATION_SEATS_SQL_TRANSACTION_H
#define VALIDATION_SEATS_SQL_TRANSACTION_H

#include "store/common/frontend/validation_transaction.h"
#include "lib/cereal/archives/binary.hpp"
#include "lib/cereal/types/string.hpp"

namespace seats_sql {

class ValidationSEATSSQLTransaction : public ValidationTransaction {
    public: 
        ValidationSEATSSQLTransaction(uint32_t timeout);
        virtual ~ValidationSEATSSQLTransaction();
};
}

#endif 