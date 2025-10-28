#include "store/benchmark/async/sql/seats/sync/seats_transaction.h"

namespace seats_sql {

SyncSEATSSQLTransaction::SyncSEATSSQLTransaction(uint32_t timeout) : SyncTransaction(timeout) {
}

SyncSEATSSQLTransaction::~SyncSEATSSQLTransaction() {
}

}
