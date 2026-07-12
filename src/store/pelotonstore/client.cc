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
#include "store/pelotonstore/client.h"
#include "store/common/util.h"

#include "store/pelotonstore/common.h"
#include "lib/cereal/archives/binary.hpp"
#include "lib/cereal/types/string.hpp"

namespace pelotonstore {

using namespace std;

Client::Client(const transport::Configuration& config, uint64_t id, int nShards, int nGroups,
      const std::vector<int> &closestReplicas,
      Transport *transport, Transport *c2cport, Partitioner *part,
      uint64_t readMessages, uint64_t readQuorumSize, bool signMessages,
      bool validateProofs, bool signClientProposals, KeyManager *keyManager, SintrParameters sintr_params,
      TrueTime timeserver,transport::Configuration *clients_config, ClientSelector *valClientSelector,
      bool fake_SMR, uint64_t SMR_mode, const std::string &PG_BFTSMART_config_path,
      const std::vector<std::string> &keys,
      bool execTxnServerSide) : config(config), nshards(nShards),
    ngroups(nGroups), transport(transport), part(part), readMessages(readMessages), readQuorumSize(readQuorumSize),
    signMessages(signMessages), validateProofs(validateProofs), signClientProposals(signClientProposals), keyManager(keyManager), timeServer(timeserver),
    sintr_params(sintr_params), fake_SMR(fake_SMR), SMR_mode(SMR_mode), PG_BFTSMART_config_path(PG_BFTSMART_config_path), txn_msg(nullptr),
    clients_config(clients_config), valClientSelector(valClientSelector), rand(id), keys(keys),
    execTxnServerSide(execTxnServerSide) {
  // just an invariant for now for everything to work ok
  assert(nGroups == nShards);

  client_id = id;

  client_seq_num = 0;

  bclient.reserve(ngroups);

  Notice("Initializing PelotonSMR client with id [%lu] %lu", client_id, ngroups);

  Notice("SignMessages: %d. ValidateProofs: %d. SignClientProposals: %d", signMessages, validateProofs, signClientProposals);

  if(ngroups > 1) Panic("Peloton store does not support sharding");

  /* Start a client for each shard. */
  for (uint64_t i = 0; i < ngroups; i++) {
    bclient[i] = new ShardClient(config, transport, client_id, i, closestReplicas,
        signMessages, validateProofs, signClientProposals, keyManager, &stats, fake_SMR, SMR_mode, PG_BFTSMART_config_path,
        sintr_params.ignorePolicyUpdate); // ignore policy update is same as setting sintr to unsafe version
  }

  policyIdFunction = GetPolicyIdFunction(sintr_params.policyFunctionName);
  policyCache = policyParseClient.ParseConfigFile(sintr_params.policyConfigPath);

  endorseClient = new EndorsementClient(client_id);
  // endorseClient->SetDebugCheckFunction(DebugCheck); TODO: make debugcheck function

  c2client = new Client2Client(clients_config, sintr_params.separateTransport ? c2cport : transport, client_id, nshards, ngroups, 0,
    signMessages, validateProofs, sintr_params, keyManager, endorseClient, valClientSelector, rand, keys);
  c2client->Init();
  waitingForEndorsementsTimeout = nullptr;
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  Notice("PelotonSMR client [%lu] created! %lu %lu", client_id, ngroups, bclient.size());
}

Client::~Client()
{
  endorsementsReceived.clear();
  if(waitingForEndorsementsTimeout != nullptr) {
    delete waitingForEndorsementsTimeout;
    waitingForEndorsementsTimeout = nullptr;
  }
    for (auto b : bclient) {
        delete b;
    }
    delete c2client;
    delete endorseClient;

    if(SMR_mode == 2) BftSmartAgent::destroy_java_vm();
}

/* Begins a transaction. All subsequent operations before a commit() or abort() are part of this transaction. */
void Client::Begin(begin_callback bcb, begin_timeout_callback btcb, uint32_t timeout, bool retry, const std::string &txnState) {
  transport->Timer(0, [this, bcb, btcb, timeout, &txnState]() {
    
    client_seq_num++;

    // no need to call delete as moved into TryCommit message
    if(txn_msg != nullptr) {
      // handle case where txn is aborted and retried
      delete txn_msg;
    }
    txn_msg = new TransactionMessage();

    // Server-side execution mode: if a txnState was provided, send a
    // TxnExecRequest now and skip all Query/Write/normal-Commit processing.
    if (execTxnServerSide && !txnState.empty()) {
      uint64_t seq = static_cast<uint64_t>(client_seq_num);
      bclient[0]->SendTxnExecRequest(
          txnState, client_id, seq,
          std::bind(&Client::HandleTxnExecReply, this, seq, std::placeholders::_1),
          timeout);
      bcb(client_seq_num);
      return;
    }

    TxnState protoTxnState;
    PolicyClient *policyClient = nullptr;
    if (sintr_params.clientEstimatePolicy) {
      policyClient = new PolicyClient();
      protoTxnState.ParseFromString(txnState);
      EstimateTxnPolicy(protoTxnState, policyClient, *policyCache, sintr_params);
    }

    perTxnPolicyIds.clear();

    Debug("BEGIN tx: ", client_seq_num);
    if(!sintr_params.ignorePolicyUpdate) {
      c2client->SendBeginValidateTxnMessage(client_seq_num, protoTxnState, std::move(policyClient));
    }
    bcb(client_seq_num);
  });
}

// TODO: consider invoke SQLRequest with the given parameters and a default db
void Client::Get(const std::string &key, get_callback gcb, get_timeout_callback gtcb, uint32_t timeout) {
  Panic("Client GET is not supported.");
}

// TODO: consider invoke SQLRequest with the given parameters and a default db
void Client::Put(const std::string &key, const std::string &value, put_callback pcb, put_timeout_callback ptcb, uint32_t timeout) {
  Panic("Client PUT is not supported.");
}


void Client::Commit(commit_callback ccb, commit_timeout_callback ctcb, uint32_t timeout) {

  transport->Timer(0, [this, ccb, ctcb, timeout]() {
    // In server-side exec mode: the TxnExecRequest was already sent during
    // Begin().  Store the commit callback keyed by seq_num so that multiple
    // open-loop transactions in flight do not stomp on each other.
    if (execTxnServerSide) {
      uint64_t seq = static_cast<uint64_t>(client_seq_num);
      auto it = pending_exec_results.find(seq);
      if (it != pending_exec_results.end()) {
        // Reply already arrived before Commit() was called.
        transaction_status_t s = it->second;
        pending_exec_results.erase(it);
        ccb(s);
      } else {
        // Reply not yet here — save the callback; HandleTxnExecReply fires it.
        pending_exec_ccbs[seq] = ccb;
      }
      return;
    }

    try_commit_callback tccb = [ccb, this](int status) {
  
      if(status == REPLY_OK) {
        Debug("COMMIT SUCCESS");
        ccb(COMMITTED);
      } else {
        Debug("COMMIT ABORT");
        ccb(ABORTED_SYSTEM);
      }
    };

    auto current_seq_num = client_seq_num;
    
    Debug("Trying to commit txn: [%lu:%lu]", client_id, client_seq_num);

    if (false) {
      Debug("FOR TRANSACTION %s", BytesToHex(TransactionDigest(*txn_msg), 16).c_str());
      for (const auto &read : txn_msg->readset()) {
        Debug("Original read key: %s", read.key().c_str());
      }
      for (const auto &write : txn_msg->writeset()) {
        Debug("Original write key: %s", write.key().c_str());
      }
    }
    if(!sintr_params.ignorePolicyUpdate) {
      endorseClient->SetExpectedTxnDigest(TransactionDigest(*txn_msg));
    }

    if(sintr_params.ignorePolicyUpdate || endorseClient->IsSatisfied()) {
      Debug("Endorsement client is already satisfied for client %d seq num %d", client_id, client_seq_num);
      getEndorsementsAndCommit(tccb, ctcb, timeout, current_seq_num);
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
      getEndorsementsAndCommit(tccb, ctcb, timeout, current_seq_num);
    }
  });
}

void Client::Abort(abort_callback acb, abort_timeout_callback atcb, uint32_t timeout) {

  transport->Timer(0, [this, acb, atcb, timeout]() {
    Debug("Issue Abort (asynchronously)");
    bclient[0]->Abort(client_id, client_seq_num);
    acb();
  });
}

void Client::SQLRequest(std::string &statement, sql_callback scb, sql_timeout_callback stcb, uint32_t timeout){

  transport->Timer(0, [this, statement, scb, stcb, timeout](){

    Debug("Invoke SQL Request: %s", statement.c_str());

    auto current_seq_id = client_seq_num;

    sql_rpc_callback srcb = [scb, statement, current_seq_id, this](
      int status, const std::string& sql_res, TransactionMessage *txn_msg, proto::SignedMessage *signedMessage
    ) {
      Debug("Received query response");

      // struct timespec ts_start;
      // clock_gettime(CLOCK_MONOTONIC, &ts_start);
      // exec_end_us = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
      // Notice("End to end exec latency: %lu us", exec_end_us - exec_start_us);
      
      //Deserialize sql_res and return to application.
      query_result::QueryResult* query_res;
      if(status == REPLY_OK) {
        Debug("Statement execution SUCCESS. Return result");
        query_res = new sql::QueryResultProtoWrapper(sql_res);

        for (auto &read : txn_msg->readset()) {
          Debug("Client read key: %s", read.key().c_str());
          if (sintr_params.includeReadsetForTxnPolicy) {
            handlePolicyUpdateOnKey(read.key());
          }
          *this->txn_msg->add_readset() = std::move(read);
        }
        for (auto &write : txn_msg->writeset()) {
          Debug("Client write key: %s", write.key().c_str());
          handlePolicyUpdateOnKey(write.key());
          *this->txn_msg->add_writeset() = std::move(write);
        }
      } else {
        Debug("Statement execution FAILURE for statement %s client ID: %lu seq num: %lu", statement.c_str(), client_id, current_seq_id);
        //This is simply a hack to force all follower replicas to also abort in order to make them unlock any held locks.
        //if(fake_SMR) bclient[0]->Abort(client_id, client_seq_num); 
        //TODO: Alternatively: Server could just abort current txn when it receives the next txn. 
        //Aborting here explicitly may release txn "earlier", but it can also introduce redundancy.
        query_res = new sql::QueryResultProtoWrapper();
        // send to validation client if
      }
      Debug("SQL GEN ID: %s for statement %s client ID: %lu seq num: %lu", BytesToHex(SQLGenId(statement, client_id, current_seq_id, sintr_params.hashQueryGenId), 16).c_str(), statement.c_str(), client_id, current_seq_id);
      Debug("SQL GEN ID: %s for seq num %lu", BytesToHex(SQLGenId(statement, client_id, client_seq_num, sintr_params.hashQueryGenId), 16).c_str(), client_seq_num);
      if(!sintr_params.ignorePolicyUpdate) {
        c2client->SendForwardSQLResultMessage(SQLGenId(statement, client_id, current_seq_id, sintr_params.hashQueryGenId), sql_res, signedMessage, txn_msg);
      } else {
        delete txn_msg;
        txn_msg = nullptr;
      }
      Debug("Upcalling");
      scb(status, query_res);
    };
    
    bclient[0]->Query(statement, client_id, client_seq_num, srcb, stcb, timeout);

  });
}


void Client::Query(const std::string &query, query_callback qcb, query_timeout_callback qtcb, uint32_t timeout,bool cache_result, bool skip_query_interpretation) {
    Debug("Processing Query Statement: %s", query.c_str());
    // In server-side exec mode the server handles all queries; just fire callback.
    if (execTxnServerSide) {
      query_result::QueryResult* query_res = new sql::QueryResultProtoWrapper();
      qcb(REPLY_OK, query_res);
      return;
    }
    this->SQLRequest(const_cast<std::string &>(query), qcb, qtcb, timeout);
}


void Client::Write(std::string &write_statement, write_callback wcb, write_timeout_callback wtcb, uint32_t timeout, bool blind_write){
    Debug("Processing Write Statement: %s", write_statement.c_str());
    // In server-side exec mode the server handles all writes; just fire callback.
    if (execTxnServerSide) {
      query_result::QueryResult* query_res = new sql::QueryResultProtoWrapper();
      wcb(REPLY_OK, query_res);
      return;
    }
    this->SQLRequest(write_statement, wcb, wtcb, timeout);
}

void Client::getEndorsementsAndCommit(try_commit_callback tccb, commit_timeout_callback ctcb, uint32_t timeout, uint64_t seq_num) {
  if (!sintr_params.ignorePolicyUpdate && !endorseClient->IsSatisfied()) {
    Debug("WAITING FOR ENDORSEMENTS HERE");
    transport->Timer(0, [this, tccb, ctcb, timeout, seq_num]() {
      getEndorsementsAndCommit(tccb, ctcb, timeout, seq_num);
    });
    return;
  }
  if(waitingForEndorsementsTimeout != nullptr) {
    delete waitingForEndorsementsTimeout;
    waitingForEndorsementsTimeout = nullptr;
  }
  UW_ASSERT(seq_num == client_seq_num);
  const auto &endorsements = sintr_params.ignorePolicyUpdate ? std::vector<std::shared_ptr<::google::protobuf::Message>>() : endorseClient->GetEndorsements();
  if(!sintr_params.ignorePolicyUpdate) {
    endorsementsReceived[seq_num] = true;
    endorseClient->SetEndorsementsUsed();
  }

  bclient[0]->Commit(client_id, seq_num, std::move(txn_msg), tccb, ctcb, timeout, endorsements);
  txn_msg = nullptr;
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

void Client::HandleTxnExecReply(uint64_t seq_num, transaction_status_t status) {
  Debug("Client::HandleTxnExecReply seq_num=%lu status=%d",
        seq_num, static_cast<int>(status));
  auto ccb_it = pending_exec_ccbs.find(seq_num);
  if (ccb_it != pending_exec_ccbs.end()) {
    // Commit() already registered its callback — fire it now.
    UW_ASSERT(ccb_it->second != nullptr);
    commit_callback ccb = std::move(ccb_it->second);
    pending_exec_ccbs.erase(ccb_it);
    ccb(status);
  } else {
    // Commit() hasn't been called yet — stash the result for it to pick up.
    pending_exec_results[seq_num] = status;
  }
}

}
