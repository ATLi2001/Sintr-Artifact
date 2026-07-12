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
#include "store/hotstuffstore/client.h"
#include "store/common/util.h"
#include "store/hotstuffstore/common.h"

namespace hotstuffstore {

using namespace std;

Client::Client(const transport::Configuration& config, uint64_t id, int nShards, int nGroups,
      const std::vector<int> &closestReplicas,
      Transport *transport, Partitioner *part,
      uint64_t readMessages, uint64_t readQuorumSize, bool signMessages,
      bool validateProofs, bool signClientProposals, KeyManager *keyManager, SintrParameters sintr_params,
      transport::Configuration *clients_config, ClientSelector *valClientSelector,
      bool order_commit, bool validate_abort,
      TrueTime timeserver, const std::vector<std::string> &keys) : config(config), nshards(nShards),
    ngroups(nGroups), transport(transport), part(part), readMessages(readMessages), readQuorumSize(readQuorumSize),
    signMessages(signMessages), sintr_params(sintr_params), clients_config(clients_config),valClientSelector(valClientSelector),
    rand(id), validateProofs(validateProofs), signClientProposals(signClientProposals), keyManager(keyManager),
    order_commit(order_commit), validate_abort(validate_abort), 
    timeServer(timeserver), keys(keys) {
  // just an invariant for now for everything to work ok
  assert(nGroups == nShards);

  client_id = id;
  // generate a random client uuid
  // client_id = 0;
  // while (client_id == 0) {
  //   random_device rd;
  //   mt19937_64 gen(rd());
  //   uniform_int_distribution<uint64_t> dis;
  //   client_id = dis(gen);
  // }
  client_seq_num = 0;

  bclient.reserve(ngroups);

  Debug("Initializing HotStuff client with id [%lu] %lu", client_id, ngroups);

  /* Start a client for each shard. */
  for (uint64_t i = 0; i < ngroups; i++) {
    bclient[i] = new ShardClient(config, transport, client_id, i, closestReplicas,
        signMessages, validateProofs, signClientProposals, keyManager, &stats, order_commit, validate_abort);
  }

  policyIdFunction = GetPolicyIdFunction(sintr_params.policyFunctionName);
  policyCache = policyParseClient.ParseConfigFile(sintr_params.policyConfigPath);

  endorseClient = new EndorsementClient(client_id);
  // endorseClient->SetDebugCheckFunction(DebugCheck); TODO: make debugcheck function

  c2client = new Client2Client(clients_config, transport, client_id, nshards, ngroups, part, 0,
    signMessages, validateProofs, sintr_params, keyManager, endorseClient, valClientSelector, rand, keys);
  c2client->Init();
  waitingForEndorsementsTimeout = nullptr;

  Debug("HotStuff client [%lu] created! %lu %lu", client_id, ngroups,
      bclient.size());
}

Client::~Client()
{
    for (auto b : bclient) {
        delete b;
    }
}

/* Begins a transaction. All subsequent operations before a commit() or
 * abort() are part of this transaction.
 */
void Client::Begin(begin_callback bcb, begin_timeout_callback btcb, uint32_t timeout, bool retry, const std::string &txnState) {
  transport->Timer(0, [this, bcb, btcb, timeout, txnState]() {
    Debug("BEGIN tx");

    client_seq_num++;
    currentTxn = proto::Transaction();
    TxnState protoTxnState;
    PolicyClient *policyClient = nullptr;
    if (sintr_params.clientEstimatePolicy) {
      policyClient = new PolicyClient();
      protoTxnState.ParseFromString(txnState);
      EstimateTxnPolicy(protoTxnState, policyClient, *policyCache, sintr_params);
    }
    perTxnPolicyIds.clear();
    // Optimistically choose a read timestamp for all reads in this transaction
    currentTxn.mutable_timestamp()->set_timestamp(timeServer.GetTime());
    currentTxn.mutable_timestamp()->set_id(client_id);
    if(!sintr_params.ignorePolicyUpdate) {
      c2client->SendBeginValidateTxnMessage(client_seq_num, protoTxnState, currentTxn.timestamp().timestamp(), std::move(policyClient));
    }
    bcb(client_seq_num);
  });
}

void Client::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {
  transport->Timer(0, [this, key, gcb, gtcb, timeout]() {
    Debug("GET [%s]", key.c_str());

    // Contact the appropriate shard to get the value.
    std::vector<int> txnGroups;
    int i = (*part)(key, nshards, -1, txnGroups) % ngroups;

    // If needed, add this shard to set of participants and send BEGIN.
    if (!IsParticipant(i)) {
      currentTxn.add_participating_shards(i);
    }

    read_callback rcb = [gcb, this](int status, const std::string &key,
        const std::string &val, const Timestamp &ts,
        const proto::SignedMessage &signedMsg, const proto::CommitProof &proof) {
      if (status == REPLY_OK) {
        ReadMessage *read = currentTxn.add_readset();
        read->set_key(key);
        ts.serialize(read->mutable_readtime());
        if (!sintr_params.ignorePolicyUpdate && sintr_params.includeReadsetForTxnPolicy) {
          handlePolicyUpdateOnKey(key);
        }
      }
      if(!sintr_params.ignorePolicyUpdate) {
        c2client->SendForwardReadResultMessage(key, val, proof, ts, signedMsg);
      }
      gcb(status, key, val, ts);
    };
    read_timeout_callback rtcb = gtcb;

    // Send the GET operation to appropriate shard.
    bclient[i]->Get(key, currentTxn.timestamp(), readMessages, readQuorumSize, rcb,
        rtcb, timeout);
  });
}

void Client::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb, uint32_t timeout) {
  transport->Timer(0, [this, key, value, pcb, ptcb, timeout]() {
    // Contact the appropriate shard to set the value.
    std::vector<int> txnGroups;
    int i = (*part)(key, nshards, -1, txnGroups) % ngroups;

    // If needed, add this shard to set of participants and send BEGIN.
    if (!IsParticipant(i)) {
      currentTxn.add_participating_shards(i);
    }

    WriteMessage *write = currentTxn.add_writeset();
    write->set_key(key);
    write->set_value(value);
    if (!sintr_params.ignorePolicyUpdate) {
      handlePolicyUpdateOnKey(key);
    }
    // Buffering, so no need to wait.
    pcb(REPLY_OK, key, value);
  });
}

void Client::Commit(commit_callback ccb, commit_timeout_callback ctcb,
    uint32_t timeout) {
  transport->Timer(0, [this, ccb, ctcb, timeout]() {
    std::string digest = TransactionDigest(currentTxn);
    auto current_seq_num = client_seq_num;
    if(!sintr_params.ignorePolicyUpdate) {
      endorseClient->SetExpectedTxnDigest(digest, current_seq_num);
    }
    if(pendingPrepares.find(digest) == pendingPrepares.end()) {
      if(sintr_params.ignorePolicyUpdate || endorseClient->IsSatisfied(current_seq_num)) {
        Debug("Endorsement client is already satisfied for client %d seq num %d", client_id, client_seq_num);
        getEndorsementsAndCommit(ccb, ctcb, timeout, current_seq_num, digest);
      } else {
        waitingForEndorsementsTimeout = new Timeout(transport, 5000, [this, current_seq_num]() {
          Debug("WAITING FOR ENDORSEMENTS TIMEOUT TRIGGERED for client %d seq num %d", client_id, current_seq_num);
          if (endorsementsReceived[current_seq_num]) {
            // check size == 0 for workload finishing edge case
            endorsementsReceived.erase(current_seq_num);
            return;
          }
          Panic("Waiting for endorsements timed out for client %d seq num %d", client_id, current_seq_num);
        });
        waitingForEndorsementsTimeout->Reset();
        getEndorsementsAndCommit(ccb, ctcb, timeout, current_seq_num, digest);
      }
    } else {
      fprintf(stderr, "already committed\n");
    }
  });
}

void Client::HandleSignedPrepareReply(std::string digest, uint64_t shard_id, int status,
  const proto::GroupedSignedMessage& gsm) {
  if (pendingPrepares.find(digest) != pendingPrepares.end()) {
    PendingPrepare* pp = &pendingPrepares[digest];

    // if(status == REPLY_OK){
    //   std::cerr << "got commit shard decision from shard_id " << shard_id << std::endl;
    // }
    // else{
    //   std::cerr << "got abort shard decision from shard_id " << shard_id << std::endl;
    // }

    if (pp->signedShardDecisions.find(shard_id) == pp->signedShardDecisions.end()) {

      pp->signedShardDecisions[shard_id] = std::move(gsm);  //instead of copying, can I move. Or release?

      // abort on even a single shard abort
      if (status != REPLY_OK) {
        proto::Transaction txn = pp->txn;
        commit_callback ccb = pp->ccb;

        //std::cerr << "ABORTING " << std::endl;
        if(validate_abort){
          proto::ShardSignedDecisions dec;
          (*dec.mutable_grouped_decisions())[shard_id] = pp->signedShardDecisions[shard_id];
          AbortTxnSigned(dec, txn, digest);
        }
        else{
          AbortTxn(txn);
        }
        pendingPrepares.erase(digest);
        ccb(ABORTED_SYSTEM);

        return;
      }

      if (pp->signedShardDecisions.size() == (uint64_t) pp->txn.participating_shards_size()) {
        //std::cerr << "COMMITTING " << std::endl;
        proto::ShardSignedDecisions dec;
        for (const auto& pair : pp->signedShardDecisions) {
          (*dec.mutable_grouped_decisions())[pair.first] = pair.second;
        }
        proto::Transaction txn = pp->txn;
        commit_callback ccb = pp->ccb;
        commit_timeout_callback ctcb = pp->ctcb;
        uint32_t timeout = pp->timeout;
        pendingPrepares.erase(digest);
        ccb(COMMITTED);
        //TODO:: remove callbacks...

        //WriteBackSigned(dec, txn, digest);

        this->WriteBackSigned(dec, txn, [](transaction_status_t tx_stat) {
          if (tx_stat != COMMITTED) {
            Panic("Writeback confirmation failed");
          }
          Debug("Got confirmation of writeback");
        }, []() {
          Debug("writeback confirmation timed out");
        }, timeout);
      }
    }
  }
}

void Client::HandlePrepareReply(std::string digest, uint64_t shard_id, int status, const proto::TransactionDecision& txndec) {
  if (pendingPrepares.find(digest) != pendingPrepares.end()) {
    PendingPrepare* pp = &pendingPrepares[digest];

    // make sure we haven't marked this shard's decision yet
    if (pp->shardDecisions.find(shard_id) == pp->shardDecisions.end()) {
      // add this shard to the list of replies
      pp->shardDecisions[shard_id] = txndec;

      // if we got an abort, tx no longer in progress
      if (status != REPLY_OK) {
        proto::Transaction txn = pp->txn;
        commit_callback ccb = pp->ccb;
        pendingPrepares.erase(digest);
        ccb(ABORTED_SYSTEM);
        AbortTxn(txn);
        return;
      }

      // wait for all callbacks to complete
      if (pp->shardDecisions.size() == (uint64_t) pp->txn.participating_shards_size()) {
        proto::ShardDecisions dec;
        for (const auto& pair : pp->shardDecisions) {
          (*dec.mutable_grouped_decisions())[pair.first] = pair.second;
        }
        commit_callback ccb = pp->ccb;
        commit_timeout_callback ctcb = pp->ctcb;
        uint32_t timeout = pp->timeout;
        proto::Transaction txn = pp->txn;
        pendingPrepares.erase(digest);
        ccb(COMMITTED);
        this->WriteBack(dec, txn, [](transaction_status_t tx_stat) {
          if (tx_stat != COMMITTED) {
            Panic("Writeback confirmation failed");
          }
          Debug("Got confirmation of writeback");
        }, []() {
          Debug("writeback confirmation timed out");
        }, timeout);
      }
    }
  }
}


void Client::WriteBackSigned(const proto::ShardSignedDecisions& dec, const proto::Transaction& txn, std::string digest) {

    for (const auto& shard_id : txn.participating_shards()) {
      bclient[shard_id]->CommitSigned(digest, dec);
    }
  }



void Client::WriteBackSigned(const proto::ShardSignedDecisions& dec, const proto::Transaction& txn,
  commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout) {
  std::string digest = TransactionDigest(txn);
  if (pendingWritebacks.find(digest) == pendingWritebacks.end()) {
    PendingWriteback pendingWriteback;
    pendingWriteback.ccb = ccb;
    pendingWriteback.txn = txn;
    pendingWritebacks[digest] = pendingWriteback;

    for (const auto& shard_id : txn.participating_shards()) {
      writeback_callback wcb = std::bind(&Client::HandleWritebackReply,
        this, digest, shard_id, std::placeholders::_1);

      writeback_timeout_callback wcbt = [](int s) {
          Debug("timeout called");
      };

      bclient[shard_id]->CommitSigned(digest, dec, wcb, wcbt, timeout);
    }
  }
}

void Client::WriteBack(const proto::ShardDecisions& dec, const proto::Transaction& txn,
  commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout) {
  std::string digest = TransactionDigest(txn);
  if (pendingWritebacks.find(digest) == pendingWritebacks.end()) {
    PendingWriteback pendingWriteback;
    pendingWriteback.ccb = ccb;
    pendingWriteback.txn = txn;
    pendingWritebacks[digest] = pendingWriteback;

    for (const auto& shard_id : txn.participating_shards()) {
      writeback_callback wcb = std::bind(&Client::HandleWritebackReply,
        this, digest, shard_id, std::placeholders::_1);

      writeback_timeout_callback wcbt = [](int s) {
          Debug("timeout called");
      };

      bclient[shard_id]->Commit(digest, dec, wcb, wcbt, timeout);
    }
  }
}

void Client::HandleWritebackReply(std::string digest, uint64_t shard_id, int status) {
  if (pendingWritebacks.find(digest) != pendingWritebacks.end()) {
    PendingWriteback* pw = &pendingWritebacks[digest];
    if (status == REPLY_FAIL) {
      commit_callback ccb = pw->ccb;
      pendingWritebacks.erase(digest);
      ccb(ABORTED_SYSTEM);
    } else {
      pw->writebackAcks.insert(shard_id);
      if (pw->writebackAcks.size() == (uint64_t) pw->txn.participating_shards_size()) {
        commit_callback ccb = pw->ccb;
        pendingWritebacks.erase(digest);
        ccb(COMMITTED);
      }
    }
  }
}

void Client::Abort(abort_callback acb, abort_timeout_callback atcb,
    uint32_t timeout) {
  transport->Timer(0, [this, acb, atcb, timeout]() {
    AbortTxn(currentTxn);
    Debug("Aborted current txn %lu", client_seq_num);
    // immediately invoke callback
    acb();
  });
}

void Client::AbortTxn(const proto::Transaction& txn) {
  stats.Increment("abort", 1);
  std::string digest = TransactionDigest(txn);
  proto::ShardSignedDecisions dec;
  for (const auto& shard_id : txn.participating_shards()) {
    bclient[shard_id]->Abort(digest, dec);
  }
}

void Client::AbortTxnSigned(const proto::ShardSignedDecisions& dec, const proto::Transaction& txn, std::string& digest){
  stats.Increment("abort", 1);
  //std::string digest = TransactionDigest(txn);
  if (pendingWritebacks.find(digest) == pendingWritebacks.end()) {
    PendingWriteback pendingWriteback;
    // pendingWriteback.ccb = ccb;
    // pendingWriteback.txn = txn;
    pendingWritebacks[digest] = pendingWriteback;
    //std::cerr << "Downcalling to Shard client to send out Abort" << '\n';
    for (const auto& shard_id : txn.participating_shards()) {
      bclient[shard_id]->Abort(digest, dec);
    }
  }

}

bool Client::IsParticipant(int g) {
  for (const auto &participant : currentTxn.participating_shards()) {
    if (participant == (uint64_t) g) {
      return true;
    }
  }
  return false;
}


void Client::getEndorsementsAndCommit(commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout, uint64_t seq_num, const std::string &digest) {
  if (!sintr_params.ignorePolicyUpdate && !endorseClient->IsSatisfied(seq_num)) {
    Debug("WAITING FOR ENDORSEMENTS HERE");
    transport->Timer(0, [this, ccb, ctcb, timeout, seq_num, digest]() {
      getEndorsementsAndCommit(ccb, ctcb, timeout, seq_num, digest);
    });
    return;
  }
  if(waitingForEndorsementsTimeout != nullptr) {
    delete waitingForEndorsementsTimeout;
    waitingForEndorsementsTimeout = nullptr;
  }
  UW_ASSERT(seq_num == client_seq_num);
  const auto &endorsements = sintr_params.ignorePolicyUpdate ? std::vector<std::shared_ptr<::google::protobuf::Message>>() : endorseClient->GetEndorsements(seq_num);
  if(!sintr_params.ignorePolicyUpdate) {
    endorsementsReceived[seq_num] = true;
    endorseClient->SetEndorsementsUsed(seq_num);
    currentTxn.set_client_id(client_id);
  }
  // add endorsements to the txn
  if (endorsements.size() > 0) {
    for (auto &endorsement : endorsements) {
      proto::SignedMessage *signedEndorsement = dynamic_cast<proto::SignedMessage*>(endorsement.get());
      if(signedEndorsement && signedEndorsement->packed_msg() != digest) {
        Warning("Endorsements not the same, %s vs original %s for sequence number %lu", BytesToHex(signedEndorsement->packed_msg(), 16).c_str(), BytesToHex(digest, 16).c_str(), seq_num);
        for (const auto &read : currentTxn.readset()) {
          Warning("Original read key: %s", BytesToHex(read.key(), 16).c_str());
        }
        for (const auto &write : currentTxn.writeset()) {
          Warning("Original write key: %s", BytesToHex(write.key(), 16).c_str());
          Warning("Original write value: %s", BytesToHex(write.value(), 16).c_str());
        }
      } else if (!signedEndorsement) {
        Panic("endorsement pointer is null");
      }
      *currentTxn.mutable_endorsements()->add_sig_msgs() = *signedEndorsement;
    }
  }
  if (false) {
    Debug("Trying to send txn: [%lu:%lu] %s", client_id, seq_num, BytesToHex(digest, 16).c_str());
    for (const auto &read : currentTxn.readset()) {
      Debug("Validation read key: %s", read.key().c_str());
    }
    for (const auto &write : currentTxn.writeset()) {
      Debug("Validation write key: %s", write.key().c_str());
      Debug("Validation write value: %s", write.value().c_str());
    }
  }

  Debug("Committing txn with seq num %lu", client_seq_num);
  PendingPrepare pendingPrepare;
  pendingPrepare.ccb = ccb;
  pendingPrepare.ctcb = ctcb;
  pendingPrepare.timeout = timeout;
  // should do a copy
  pendingPrepare.txn = currentTxn;
  pendingPrepares[digest] = pendingPrepare;

  if (currentTxn.participating_shards_size() == 0) {
    fprintf(stderr, "0 participating shards\n");
  }
  stats.Increment("called_commit",1);
  stats.IncrementList("txn_groups", currentTxn.participating_shards_size());

  for (const auto& shard_id : currentTxn.participating_shards()) {

    prepare_timeout_callback pcbt = [](int s) {
      Debug("prepare timeout called");
    };
    if (signMessages) {
      signed_prepare_callback pcb = std::bind(&Client::HandleSignedPrepareReply,
        this, digest, shard_id, std::placeholders::_1, std::placeholders::_2);

      bclient[shard_id]->SignedPrepare(currentTxn, pcb, pcbt, timeout);
    } else {
      prepare_callback pcb = std::bind(&Client::HandlePrepareReply,
        this, digest, shard_id, std::placeholders::_1, std::placeholders::_2);

      bclient[shard_id]->Prepare(currentTxn, pcb, pcbt, timeout);
    }
  }
}

void Client::handlePolicyUpdateOnKey(const std::string &key) {
  if(!sintr_params.ignorePolicyUpdate) {
    // TODO: need to also handle policy change functions
    std::string policyId = policyIdFunction(key, "");
    if (perTxnPolicyIds.find(policyId) == perTxnPolicyIds.end()) {
      perTxnPolicyIds.insert(policyId);
      const Policy *policy = policyCache->Get(policyId);
      if(policy == nullptr) {
        Panic("Policy for policy id %s not found in policy cache", policyId.c_str());
      }
      Debug("handle policy update for policy id %s in write", policyId.c_str());
      c2client->HandlePolicyUpdate(policy);
    }
  }
}

}
