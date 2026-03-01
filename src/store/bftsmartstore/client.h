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
#ifndef _BFTSMART_CLIENT_H_
#define _BFTSMART_CLIENT_H_

#include "lib/assert.h"
#include "lib/keymanager.h"
#include "lib/message.h"
#include "lib/configuration.h"
#include "lib/udptransport.h"
#include "replication/ir/client.h"
#include "store/common/timestamp.h"
#include "store/common/truetime.h"
#include "store/common/frontend/client.h"
#include "store/bftsmartstore/pbft-proto.pb.h"
#include "store/bftsmartstore/shardclient.h"
#include "store/bftsmartstore/client2client.h"
#include "store/common/sintring/endorsement_client.h"
#include "store/common/policy/policy-proto.pb.h"
#include "store/common/policy/policy_parse_client.h"
#include "store/common/policy/policy_function.h"
#include "store/common/policy/client_selector.h"
#include "store/common/policy/policy_cache.h"
#include "store/common/sintring/params.h"
#include "store/common/sintring/client_common.h"

#include <unordered_map>

namespace bftsmartstore {

class Client : public ::Client {
 public:
  Client(const transport::Configuration& config, uint64_t id, int nShards, int nGroups,
      const std::vector<int> &closestReplicas,
      Transport *transport, Partitioner *part,
      uint64_t readMessages, uint64_t readQuorumSize, bool signMessages,
      bool validateProofs, bool signClientProposals, KeyManager *keyManager, const std::string& bftsmart_config_path, SintrParameters sintr_params,
      transport::Configuration *clients_config = nullptr, ClientSelector *valClientSelector = nullptr,
      bool order_commit = false, bool validate_abort = false,
      TrueTime timeserver = TrueTime(0,0), const std::vector<std::string> &keys = std::vector<std::string>(),
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

 private:
   uint64_t start_time;

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
  int client_seq_num;
  const std::string& bftsmart_config_path;

  //addtional knobs: 1) order commit, 2) validate abort
  bool order_commit = false;
  bool validate_abort = false;

  // When true, Begin() serialises the TxnState and forwards it to the server
  // via a TxnExecRequest; Get/Put become no-ops and Commit waits for the reply.
  bool execTxnServerSide = false;
  // Per-seq_num commit callbacks and early-arriving results (open-loop safe).
  std::unordered_map<uint64_t, commit_callback> pending_exec_ccbs;
  std::unordered_map<uint64_t, transaction_status_t> pending_exec_results;

  struct PendingPrepare {
    proto::Transaction txn;
    // collected decisions from each shard
    std::unordered_map<uint64_t, proto::TransactionDecision> shardDecisions;
    std::unordered_map<uint64_t, proto::GroupedSignedMessage> signedShardDecisions;

    commit_callback ccb;
    commit_timeout_callback ctcb;
    uint32_t timeout;
  };

  struct PendingWriteback {
    proto::Transaction txn;
    // set of replicas we got a writeback from
    std::unordered_set<uint64_t> writebackAcks;

    commit_callback ccb;
  };

  void HandleSignedPrepareReply(std::string digest, uint64_t shard_id, int status, const proto::GroupedSignedMessage& gsm);

  void HandlePrepareReply(std::string digest, uint64_t shard_id, int status, const proto::TransactionDecision& txndec);

  void HandleWritebackReply(std::string digest, uint64_t shard_id, int status);

  // Handles a TxnExecReply from the server (server-side execution mode).
  void HandleTxnExecReply(uint64_t seq_num, transaction_status_t status);

  // Current transaction.
  proto::Transaction currentTxn;

  // map from txn digest to pending prepare state
  std::unordered_map<std::string, PendingPrepare> pendingPrepares;

  // map from txn digest to pending writeback state
  std::unordered_map<std::string, PendingWriteback> pendingWritebacks;

  /* Debug State */
  std::unordered_map<std::string, uint32_t> statInts;

  void WriteBackSigned(const proto::ShardSignedDecisions& dec, const proto::Transaction& txn, std::string digest);

  void WriteBackSigned(const proto::ShardSignedDecisions& dec, const proto::Transaction& txn,
    commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout);

  void WriteBack(const proto::ShardDecisions& dec, const proto::Transaction& txn,
    commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout);

  void AbortTxnSigned(const proto::ShardSignedDecisions& dec, const proto::Transaction& txn, std::string& digest);

  void AbortTxn(const proto::Transaction& txn);

  bool IsParticipant(int g);

  //SINTR STUFF

  void getEndorsementsAndCommit(commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout, uint64_t seq_num, const std::string &digest);

  void handlePolicyUpdateOnKey(const std::string &key);

  // client for other clients
  Client2Client *c2client;
  const std::vector<std::string> &keys;
  EndorsementClient *endorseClient;
  PolicyParseClient policyParseClient;
  policy_id_function policyIdFunction;
  std::unique_ptr<PolicyCache> policyCache;
  std::mt19937 rand;

  ClientSelector *valClientSelector;
  SintrParameters sintr_params;
  std::unordered_map<uint64_t, bool> endorsementsReceived;
  Timeout *waitingForEndorsementsTimeout;
  std::unordered_set<std::string> perTxnPolicyIds;
};

} // namespace bftsmartstore

#endif /* _BFTSMART_CLIENT_H_ */
