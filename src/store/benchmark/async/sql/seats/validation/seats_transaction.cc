#include "store/benchmark/async/sql/seats/validation/seats_transaction.h"

namespace seats_sql {

ValidationSEATSSQLTransaction::ValidationSEATSSQLTransaction(uint32_t timeout) : ValidationTransaction(timeout) {
}

ValidationSEATSSQLTransaction::~ValidationSEATSSQLTransaction() {
}

}
