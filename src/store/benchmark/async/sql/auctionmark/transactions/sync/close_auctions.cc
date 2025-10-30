/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Liam Arzola <lma77@cornell.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/
#include "store/benchmark/async/sql/auctionmark/transactions/sync/close_auctions.h"


namespace auctionmark {

SyncCloseAuctions::SyncCloseAuctions(uint32_t timeout, AuctionMarkProfile &profile, std::mt19937_64 &gen) : 
  CloseAuctions(timeout, profile, gen), AuctionMarkSyncTransaction(timeout) {}

SyncCloseAuctions::~SyncCloseAuctions() {}

transaction_status_t SyncCloseAuctions::Execute(SyncClient &client) {
  Panic("Pequinstore cannot yet implement CloseAuctions with rounds>1 as this requires read-your-own-write semantics (Next auction round needs to reflect the new status, or else we will re-read the same)");
  return transaction_status_t::COMMITTED;
}

} // namespace auctionmark
