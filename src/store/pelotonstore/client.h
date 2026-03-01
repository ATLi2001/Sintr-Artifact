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
#ifndef _PELOTON_CLIENT_H_
#define _PELOTON_CLIENT_H_

#include "lib/assert.h"
#include "lib/keymanager.h"
#include "lib/message.h"
#include "lib/configuration.h"
#include "lib/udptransport.h"
#include "replication/ir/client.h"
#include "store/common/timestamp.h"
#include "store/common/truetime.h"
#include "store/common/frontend/client.h"
#include "store/pelotonstore/pbft-proto.pb.h"
#include "store/common/query_result/query-result-proto.pb.h"
#include "store/pelotonstore/shardclient.h"
#include "store/common/query_result/query_result.h"
#include "store/common/query_result/query_result_proto_wrapper.h"
#include "store/pelotonstore/client2client.h"
#include "store/common/sintring/endorsement_client.h"
#include "store/common/policy/policy-proto.pb.h"
#include "store/common/policy/policy_parse_client.h"
#include "store/common/policy/policy_function.h"
#include "store/common/policy/client_selector.h"
#include "store/common/policy/policy_cache.h"
#include "store/common/sintring/params.h"
#include "store/common/sintring/client_common.h"

#include <unordered_map>

#include <tao/pq.hpp> //Test connecting directly
#include "store/common/query_result/taopq_query_result_wrapper.h"

namespace pelotonstore {

class Client : public ::Client {
 public:
  Client(const transport::Configuration& config, uint64_t id, int nShards, int nGroups,
      const std::vector<int> &closestReplicas,
      Transport *transport, Transport *c2cport, Partitioner *part,
      uint64_t readMessages, uint64_t readQuorumSize, bool signMessages,
      bool validateProofs, bool signClientProposals, KeyManager *keyManager,
      SintrParameters sintr_params, TrueTime timeserver = TrueTime(0,0),
      transport::Configuration *clients_config = nullptr, ClientSelector *valClientSelector = nullptr,
      bool fake_SMR = true, uint64_t SMR_mode = 0, const std::string &PG_BFTSMART_config_path = "",
      const std::vector<std::string> &keys = std::vector<std::string>(),
      bool execTxnServerSide = false);
  ~Client();

  // Begin a transaction.
  virtual void Begin(begin_callback bcb, begin_timeout_callback btcb,
      uint32_t timeout, bool retry = false, const std::string &txnState = std::string()) override;

  // Get the value corresponding to key.
  virtual void Get(const std::string &key, get_callback gcb,
      get_timeout_callback gtcb, uint32_t timeout) override;

  // Set the value for the given key.
  virtual void Put(const std::string &key, const std::string &value,
      put_callback pcb, put_timeout_callback ptcb,
      uint32_t timeout) override;

  // Commit all Get(s) and Put(s) since Begin().
  virtual void Commit(commit_callback ccb, commit_timeout_callback ctcb,
      uint32_t timeout) override;

  // Abort all Get(s) and Put(s) since Begin().
  virtual void Abort(abort_callback acb, abort_timeout_callback atcb,
      uint32_t timeout) override;

  virtual void SQLRequest(std::string &statement, sql_callback scb, sql_timeout_callback stcb, uint32_t timeout) override;

  // Perform the given query.
  virtual void Query(const std::string &query, query_callback qcb,
      query_timeout_callback qtcb, uint32_t timeout, bool cache_result = false, bool skip_query_interpretation = false) override;

  virtual void Write(std::string &write_statement, write_callback wcb,write_timeout_callback wtcb, uint32_t timeout, bool blind_write = false) override;

 private:
  void getEndorsementsAndCommit(try_commit_callback tccb, commit_timeout_callback ctcb, uint32_t timeout, uint64_t seq_num);
  void handlePolicyUpdateOnKey(const std::string &key);
  std::shared_ptr<tao::pq::connection> connection;
  std::shared_ptr<tao::pq::transaction> transaction;

  std::shared_ptr<tao::pq::connection_pool> connectionPool;


  uint64_t client_id;
  /* Configuration State */
  transport::Configuration config;
  // client to client transport configuration state
  transport::Configuration *clients_config;
  // Number of replica groups.
  uint64_t nshards;
  // Number of replica groups.
  uint64_t ngroups;
  // Transport used by shard clients.
  Transport *transport;
  // Client for each shard
  std::vector<ShardClient *> bclient;
  Partitioner *part;
  uint64_t readMessages;
  uint64_t readQuorumSize;
  bool signMessages;
  bool validateProofs;
  bool signClientProposals;
  KeyManager *keyManager;
  // TrueTime server.
  TrueTime timeServer;
  uint64_t client_seq_num;

   // If this flag is set, then we are simulating a fake SMR in which we only care about the reply from a single replica ("leader").
  //We use this to simulate an upper bound of performance that would be achievable with a parallel SMR execution engine (akin to Block-STM)
  bool fake_SMR;
  uint64_t SMR_mode; //Control whether to run without replication (0), with Hotstuff (1) or BFTSmart (2)
  const std::string& PG_BFTSMART_config_path; //Path for BFTSmart (if in use)

  // When true, Begin() serialises the TxnState and forwards it to the server
  // via a TxnExecRequest; Query/Write become no-ops and Commit waits for the reply.
  bool execTxnServerSide = false;
  // Per-seq_num commit callbacks and early-arriving results (open-loop safe).
  std::unordered_map<uint64_t, commit_callback> pending_exec_ccbs;
  std::unordered_map<uint64_t, transaction_status_t> pending_exec_results;

  // Handles a TxnExecReply from the server (server-side execution mode).
  void HandleTxnExecReply(uint64_t seq_num, transaction_status_t status);

  // track overall readset and writeset of transaction
  TransactionMessage *txn_msg;
  std::unordered_set<std::string> perTxnPolicyIds;


  /* Debug State */
  std::unordered_map<std::string, uint32_t> statInts;

  uint64_t exec_start_us;
  uint64_t exec_end_us;
  // client for other clients
  Client2Client *c2client;

  // collect endorsements for current transaction
  EndorsementClient *endorseClient;
  PolicyParseClient policyParseClient;
  policy_id_function policyIdFunction;
  std::unique_ptr<PolicyCache> policyCache;

  ClientSelector *valClientSelector;
  SintrParameters sintr_params;
  const std::vector<std::string> &keys;
  std::mt19937 rand;
  std::unordered_map<uint64_t, bool> endorsementsReceived;
  Timeout *waitingForEndorsementsTimeout;
};

} // namespace pelotonstore

#endif /* _PELOTON_CLIENT_H_ */
