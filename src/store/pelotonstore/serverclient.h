/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Yunhao Zhang <yz2327@cornell.edu>
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
#ifndef _PELOTON_SERVERCLIENT_H_
#define _PELOTON_SERVERCLIENT_H_

#include "lib/assert.h"
#include "lib/message.h"
#include "store/common/timestamp.h"
#include "store/common/truetime.h"
#include "store/common/frontend/client.h"
#include "store/pelotonstore/server.h"

#include <unordered_map>
#include <unordered_set>

namespace pelotonstore {

// ServerClient bypasses shard networking entirely and calls directly into
// a co-located Server instance.  Instead of Get/Put (key-value), it forwards
// Query/Write (SQL) calls to the server's table_store, exactly mirroring what
// HandleSQL_RPC does for normal client requests.
class ServerClient : public ::Client {
 public:
  ServerClient(Server *server, uint64_t id, Transport *transport,
      TrueTime timeserver = TrueTime(0, 0));
  ~ServerClient();

  // Begin a transaction.
  virtual void Begin(begin_callback bcb, begin_timeout_callback btcb,
      uint32_t timeout, bool retry = false,
      const std::string &txnState = std::string()) override;

  // Get is not used for SQL benchmarks but must be implemented.
  virtual void Get(const std::string &key, get_callback gcb,
      get_timeout_callback gtcb, uint32_t timeout) override;

  // Put is not used for SQL benchmarks but must be implemented.
  virtual void Put(const std::string &key, const std::string &value,
      put_callback pcb, put_timeout_callback ptcb,
      uint32_t timeout) override;

  // Commit the current transaction.
  virtual void Commit(commit_callback ccb, commit_timeout_callback ctcb,
      uint32_t timeout) override;

  // Abort the current transaction.
  virtual void Abort(abort_callback acb, abort_timeout_callback atcb,
      uint32_t timeout) override;

  // Execute a SQL query (read).
  virtual void Query(const std::string &query_statement, query_callback qcb,
      query_timeout_callback qtcb, uint32_t timeout,
      bool cache_result = false, bool skip_query_interpretation = false) override;

  // Execute a SQL write statement.
  virtual void Write(std::string &write_statement, write_callback wcb,
      write_timeout_callback wtcb, uint32_t timeout,
      bool blind_write = false) override;

 private:
  Server *server;
  uint64_t client_id;
  Transport *transport;
  TrueTime timeServer;
  uint64_t client_seq_num;

  // Execute a SQL statement against the server's table_store, returning
  // the serialised result string. Updates result_status accordingly.
  std::string ExecSQL(const std::string &sql_statement);
};

} // namespace pelotonstore

#endif /* _PELOTON_SERVERCLIENT_H_ */
