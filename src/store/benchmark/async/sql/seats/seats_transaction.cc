#include "store/benchmark/async/sql/seats/seats_transaction.h"

namespace seats_sql {

SEATSSQLTransaction::SEATSSQLTransaction(uint32_t timeout) : timeout(timeout) {
}

SEATSSQLTransaction::~SEATSSQLTransaction() {
}

}
