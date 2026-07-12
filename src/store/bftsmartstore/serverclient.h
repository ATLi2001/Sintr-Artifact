/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Zheng Wang <zw494@cornell.edu>
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
#ifndef _BFTSMART_SERVERCLIENT_H_
#define _BFTSMART_SERVERCLIENT_H_

#include "lib/assert.h"
#include "lib/message.h"
#include "store/common/timestamp.h"
#include "store/common/truetime.h"
#include "store/common/frontend/client.h"
#include "store/bftsmartstore/pbft-proto.pb.h"
#include "store/bftsmartstore/server.h"

#include <unordered_map>
#include <unordered_set>

namespace bftsmartstore {

// ServerClient bypasses shard networking entirely and calls directly into
// a co-located Server instance. No ShardClient, partitioner, or transport
// sharding is needed.
class ServerClient : public ::Client {
 public:
  // server  - the Server instance running on this machine
  // id      - client id (used to stamp transaction timestamps)
  // transport - transport for scheduling deferred callbacks (Timer)
  ServerClient(Server *server, uint64_t id, Transport *transport,
      TrueTime timeserver = TrueTime(0, 0));
  ~ServerClient();

  virtual void Begin(begin_callback bcb, begin_timeout_callback btcb,
      uint32_t timeout, bool retry = false,
      const std::string &txnState = std::string()) override;

  virtual void Get(const std::string &key, get_callback gcb,
      get_timeout_callback gtcb, uint32_t timeout) override;

  virtual void Put(const std::string &key, const std::string &value,
      put_callback pcb, put_timeout_callback ptcb,
      uint32_t timeout) override;

  virtual void Commit(commit_callback ccb, commit_timeout_callback ctcb,
      uint32_t timeout) override;

  virtual void Abort(abort_callback acb, abort_timeout_callback atcb,
      uint32_t timeout) override;

 private:
  Server *server;
  uint64_t client_id;
  Transport *transport;
  TrueTime timeServer;
  int client_seq_num;
  uint64_t read_req_id;

  proto::Transaction currentTxn;

  void AbortTxn(const proto::Transaction &txn);
};

} // namespace bftsmartstore

#endif /* _BFTSMART_SERVERCLIENT_H_ */
