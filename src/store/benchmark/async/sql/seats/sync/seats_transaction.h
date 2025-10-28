#ifndef SYNC_SEATS_SQL_TRANSACTION_H
#define SYNC_SEATS_SQL_TRANSACTION_H

#include "store/common/frontend/sync_client.h"
#include "store/common/frontend/sync_transaction.h"
#include "lib/cereal/archives/binary.hpp"
#include "lib/cereal/types/string.hpp"

namespace seats_sql {

class SyncSEATSSQLTransaction : public SyncTransaction {
    public: 
        SyncSEATSSQLTransaction(uint32_t timeout);
        virtual ~SyncSEATSSQLTransaction();
};
}

#endif 