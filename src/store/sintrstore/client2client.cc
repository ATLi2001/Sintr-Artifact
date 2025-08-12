// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/sintr/client2client.cc:
 *   Sintr client to client.
 *
 * Copyright 2024 Austin Li <atl63@cornell.edu>
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

#include "store/sintrstore/client2client.h"
#include "store/sintrstore/basicverifier.h"
#include "store/sintrstore/validation_client.h"
#include "store/common/frontend/validation_transaction.h"
#include "store/benchmark/async/tpcc/tpcc-validation-proto.pb.h"
#include "store/sintrstore/common.h"
#include "store/sintrstore/common2.h"

#include <google/protobuf/util/message_differencer.h>
#include <sched.h>
#include <pthread.h>
#include <memory>

namespace sintrstore {

Client2Client::Client2Client(transport::Configuration *config, transport::Configuration *clients_config, Transport *transport,
      uint64_t client_id, uint64_t nshards, uint64_t ngroups, int group, bool pingClients,
      Parameters params, KeyManager *keyManager, Verifier *verifier,
      Partitioner *part, EndorsementClient *endorseClient, SQLTransformer *sql_interpreter, std::string &table_registry,
      ClientSelector *valClientSelector, std::mt19937 &rand,
      const std::vector<std::string> &keys) :
      PingInitiator(this, transport, clients_config->n),
      client_id(client_id), transport(transport), config(config), clients_config(clients_config), 
      nshards(nshards), ngroups(ngroups),
      group(group), part(part), pingClients(pingClients), params(params),
      keyManager(keyManager), verifier(verifier), endorseClient(endorseClient), sql_interpreter(sql_interpreter),
      keys(keys), valClientSelector(valClientSelector), rand(rand) {
  
  // separate verifier from main client instance
  clients_verifier = new BasicVerifier(transport);
  
  valClient = new ValidationClient(transport, client_id, clients_config->n, nshards, ngroups, part, table_registry, params); 
  valParseClient = new ValidationParseClient(10000, keys); // TODO: pass arg for timeout length
  transport->Register(this, *clients_config, group, client_id); 

  // assume these are somehow secretly shared before hand
  uint64_t idx = client_id;
  for (uint64_t i = 0; i < clients_config->n; i++) {
    if (i > idx) {
      sessionKeys[i] = std::string(8, (char) idx + 0x30) + std::string(8, (char) i + 0x30);
    } else {
      sessionKeys[i] = std::string(8, (char) i + 0x30) + std::string(8, (char) idx + 0x30);
    }
  }

  done = false;

  // // for each client process, have 1 core for main client thread and maxValThreads for validation threads
  // // if multi-threading message processing, need to reserve 1 more core per client
  // // so each client process takes up a total of maxValThreads + (1 or 2) cores
  // int num_cpus = std::thread::hardware_concurrency();
  // int main_client_cpu;
  // if (params.sintr_params.c2cReceiveThread) {
  //   main_client_cpu = client_id * (params.sintr_params.maxValThreads + 2) % num_cpus;
  // }
  // else {
  //   main_client_cpu = client_id * (params.sintr_params.maxValThreads + 1) % num_cpus;
  // }

  // each process gets 2 cpus, one for main client thread and one for all validation, send, receive, sig check threads 
  int num_cpus = std::thread::hardware_concurrency();
  size_t cpus_per_client = 2;
  // if we give more sig check threads, up to 4 cpus per client
  if (params.sintr_params.maxClientSigCheckThreads > 0) {
    cpus_per_client = 4;
  }
  int main_client_cpu = (client_id * cpus_per_client) % num_cpus;

  Debug("Starting %lu validation threads", params.sintr_params.maxValThreads);
  for (size_t i = 0; i < params.sintr_params.maxValThreads; i++) {
    valThreads.push_back(new std::thread(&Client2Client::ValidationThreadFunction, this));
    if (params.sintr_params.clientPinCores) {
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + (1 + i) % cpus_per_client) % num_cpus, &cpuset);
      pthread_setaffinity_np(valThreads[i]->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }

  if (params.sintr_params.c2cSendThread) {
    Debug("Starting c2cSendThread");
    c2cSendThread = new std::thread(&Client2Client::Client2ClientExecutorThreadFunction, this, std::ref(c2cSendQueue));
    if (params.sintr_params.clientPinCores) {
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + 1) % num_cpus, &cpuset);
      pthread_setaffinity_np(c2cSendThread->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }
  if (params.sintr_params.c2cReceiveThread) {
    Debug("Starting c2cReceiveThread");
    c2cReceiveThread = new std::thread(&Client2Client::Client2ClientExecutorThreadFunction, this, std::ref(c2cReceiveQueue));
    if (params.sintr_params.clientPinCores) {
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + 1) % num_cpus, &cpuset);
      pthread_setaffinity_np(c2cReceiveThread->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }

  for (size_t i = 0; i < params.sintr_params.maxClientSigCheckThreads; i++) {
    parallelSigCheckThreads.push_back(
      new std::thread(&Client2Client::Client2ClientExecutorThreadFunction, this, std::ref(parallelSigCheckQueue))
    );
    if (params.sintr_params.clientPinCores) {
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + (2 + i) % cpus_per_client) % num_cpus, &cpuset);
      pthread_setaffinity_np(parallelSigCheckThreads[i]->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }

  client_time_to_endorse_us.resize(clients_config->n);
}

Client2Client::~Client2Client() {
  done = true;
  // send a dummy message to unblock any waiting threads before joining
  for (auto t : valThreads) {
    validationQueue.push(nullptr);
  }
  for (auto t : valThreads) {
    t->join();
    delete t;
  }
  if (params.sintr_params.c2cSendThread) {
    c2cSendQueue.push(nullptr);
    c2cSendThread->join();
    delete c2cSendThread;
  }
  if (params.sintr_params.c2cReceiveThread) {
    c2cReceiveQueue.push(nullptr);
    c2cReceiveThread->join();
    delete c2cReceiveThread;
  }
  for (auto t : parallelSigCheckThreads) {
    parallelSigCheckQueue.push(nullptr);
  }
  for (auto t : parallelSigCheckThreads) {
    t->join();
    delete t;
  }
  delete valClient;
  delete clients_verifier;
  delete valParseClient;
}

void Client2Client::ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data, void *meta_data) {

  if (type == ping.GetTypeName()) {
    Debug("ping received");
    ping.ParseFromString(data);
    HandlePingMessage(ping);
  }
  else if (type == beginValTxnMsg.GetTypeName()) {
    ManageDispatchBeginValidateTxnMessage(remote, data);
  }
  else if (type == fwdReadResultMsg.GetTypeName()) {
    ManageDispatchForwardReadResultMessage(remote, data);
  }
  else if (type == fwdPointQueryResultMsg.GetTypeName()) {
    ManageDispatchForwardPointQueryResultMessage(remote, data);
  }
  else if (type == fwdQueryResultMsg.GetTypeName()) {
    ManageDispatchForwardQueryResultMessage(remote, data);
  }
  else if (type == blindWriteMsg.GetTypeName()) {
    ManageDispatchBlindWriteMessage(remote, data);
  }  
  else if (type == finishValTxnMsg.GetTypeName()) {
    ManageDispatchFinishValidateTxnMessage(remote, data);
  }
  else {
    Panic("Received unexpected message type: %s", type.c_str());
  }
}

bool Client2Client::SendPing(size_t replica, const PingMessage &ping) {
  // do not ping self
  if (replica != client_id) {
    transport->SendMessageToReplica(this, group, replica, ping);
  }
  return true;
}

bool Client2Client::MySendPing(size_t replica, const PingMessage &ping, bool initiator) {  
  if (replica != client_id) {
    if (initiator) {
      if (ping_rtt_us.count > 0 && ping_rtt_us.count % 400 == 0) {
        std::cerr << "Mean ping rtt: " << ping_rtt_us.mean() << std::endl;
      }
      Debug("Initiating ping to client %lu", replica);
      struct timespec ts_start;
      clock_gettime(CLOCK_MONOTONIC, &ts_start);
      ping_begin_time_us = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
    }
    else {
      Debug("Replying to ping from client %lu", replica);
    }
    transport->SendMessageToReplica(this, replica, ping);
  }
  return true;
}

void Client2Client::HandlePingMessage(const PingMessage &ping) {
  // someone else's ping
  if (ping.salt() != client_id) {
    MySendPing(ping.salt(), ping, false);
  }
  else {
    // our own ping
    Debug("Received own ping");
    struct timespec ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - send_begin_time_us;

    // if (num_pings >= time_to_begin_ack_n_us.size()) {
    //   time_to_begin_ack_n_us.resize(num_pings + 1);
    // }
    // time_to_begin_ack_n_us[num_pings].add(duration);
    // num_pings++;

    // auto duration = end - ping_begin_time_us;
    // ping_rtt_us.add(duration);
  }
}

void Client2Client::SendBeginValidateTxnMessage(uint64_t client_seq_num, const TxnState &protoTxnState, uint64_t txnStartTime,
    PolicyClient *policyClient) {

  if (params.sintr_params.clientEstimatePolicy) {
    UW_ASSERT(policyClient != nullptr);
  }
  else {
    // no estimate, so no need to send any begin validate messages
    UW_ASSERT(policyClient == nullptr);
    // still some bookkeeping to do
    ResetTrackingState();
    this->client_seq_num = client_seq_num;
    beginValSent.insert(client_id);
    return;
  }
  
  if (!params.sintr_params.c2cSendThread) {
    SendBeginValidateTxnMessageHelper(client_seq_num, protoTxnState, txnStartTime, policyClient);
    delete policyClient;
  }
  else {
    auto f = [=]() {
      this->SendBeginValidateTxnMessageHelper(
        client_seq_num, protoTxnState, txnStartTime, policyClient
      );
      delete policyClient;
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

void Client2Client::SendBeginValidateTxnMessageHelper(const uint64_t client_seq_num, const TxnState &protoTxnState,
    uint64_t txnStartTime, PolicyClient *policyClient) {
  UW_ASSERT(policyClient != nullptr);

  // if (create_hmac_us.count > 0 && create_hmac_us.count % 2000 == 0) {
  //   std::cerr << "Mean create HMAC latency: " << create_hmac_us.mean() << std::endl;
  //   std::cerr << "Mean verify endorsement latency: " << verify_endorse_us.mean() << std::endl;
  // }
  // if (fwd_read_to_receive_endorse_us.count > 0 && fwd_read_to_receive_endorse_us.count % 2000 == 0) {
  //   std::cerr << "Mean fwd read to receive endorsement latency: " << fwd_read_to_receive_endorse_us.mean() << std::endl;
  // }
  // if (fwd_point_query_to_receive_endorse_us.count > 0 && fwd_point_query_to_receive_endorse_us.count % 2000 == 0) {
  //   std::cerr << "Mean fwd point query to receive endorsement latency: " << fwd_point_query_to_receive_endorse_us.mean() << std::endl;
  // }
  // if (send_begin_to_receive_endorse_us.count > 0 && send_begin_to_receive_endorse_us.count % 1000 == 0) {
  //   std::cerr << "Mean send begin to receive endorsement latency: " << send_begin_to_receive_endorse_us.mean() << std::endl;
  // }
  // if (time_to_endorse_n_us.size() > 0 && time_to_endorse_n_us[0].count % 2000 == 0) {
  //   for (size_t i = 0; i < time_to_endorse_n_us.size(); i++) {
  //     std::cerr << "Mean time to receive endorsement " << i << ": " << time_to_endorse_n_us[i].mean() << std::endl;
  //   }

  //   for (size_t i = 0; i < client_time_to_endorse_us.size(); i++) {
  //     if (client_time_to_endorse_us[i].count > 0) {
  //       std::cerr << "Mean time to receive endorsement from client " << i << ": " << client_time_to_endorse_us[i].mean() << std::endl;
  //     }
  //   }
  // }

  // if (time_to_begin_ack_n_us.size() > 0 && time_to_begin_ack_n_us[0].count % 2000 == 0) {
  //   for (size_t i = 0; i < time_to_begin_ack_n_us.size(); i++) {
  //     std::cerr << "Mean time to receive begin ack " << i << ": " << time_to_begin_ack_n_us[i].mean() << std::endl;
  //   }
  // }

  ResetTrackingState();
  this->client_seq_num = client_seq_num;
  // for tracking purposes, must have self in beginValSent
  beginValSent.insert(client_id);

  sentBeginValTxnMsg.Clear();
  proto::BeginValidateTxn beginValTxn;
  beginValTxn.set_client_id(client_id);
  beginValTxn.set_client_seq_num(client_seq_num);
  *beginValTxn.mutable_txn_state() = protoTxnState;
  if(params.sintr_params.hideTimestamps) {
    beginValTxn.set_hashed_ts(TimestampDigest(client_id, txnStartTime));
  } else {
    beginValTxn.mutable_timestamp()->set_timestamp(txnStartTime);
    beginValTxn.mutable_timestamp()->set_id(client_id);
  }

  if (params.sintr_params.signFwdReadResults) {
    CreateHMACedMessage(
      beginValTxn,
      *sentBeginValTxnMsg.mutable_signed_begin_validate_txn()
    );
  }
  else {
    *sentBeginValTxnMsg.mutable_begin_validate_txn() = std::move(beginValTxn);
  }

  Debug("beginValTxnMsg client id %lu, seq num %lu", client_id, client_seq_num);

  // num_pings = 0;
  // ping to test RTT
  // if (client_seq_num % 20 == 0) {
  //   ping.set_salt(client_id);
  //   uint64_t target = (client_id + 1) % clients_config->n;
  //   MySendPing(target, ping);
  // }
  // return;

  // send to all clients so no need to bother with estimated policy
  if(params.sintr_params.clientValidationHeuristic == CLIENT_VALIDATION_HEURISTIC::ALL) {
    for (int i = 0; i < clients_config->n; i++) {
      // do not send to self
      if (i == client_id) {
        continue;
      }
      beginValSent.insert(i);
      transport->SendMessageToReplica(this, i, sentBeginValTxnMsg);
    }
  }
  // other heuristics depend on actual policy that was estimated
  else {
    // precompute the order of clients to contact
    if (valClientSelector != nullptr) {
      // false = without replacement
      valClientOrder = valClientSelector->GetClientIds(rand, clients_config->n, false);
    }

    // extract out the clients that need to be contacted
    std::set<uint64_t> clients;
    // need to use DifferenceToSatisfied to account for self
    ExtractFromPolicyClientsToContact(policyClient->DifferenceToSatisfied(beginValSent), clients);
    
    if (params.sintr_params.clientValidationHeuristic == CLIENT_VALIDATION_HEURISTIC::EXACT) {
    }
    else if (params.sintr_params.clientValidationHeuristic == CLIENT_VALIDATION_HEURISTIC::ONE_MORE) {
      for (int i = 0; i < clients_config->n; i++) {
        if (i != client_id && clients.find(i) == clients.end()) {
          clients.insert(i);
        }
      }
    }
    else {
      Panic("Invalid clientValidationHeuristic value");
    }

    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // send_begin_time_us = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    for (const auto &i : clients) {
      // do not send to self
      if (i == client_id) {
        continue;
      }
      beginValSent.insert(i);
      transport->SendMessageToReplica(this, i, sentBeginValTxnMsg);
    }
    // sanity check - policy should be satisfied by the clients we are sending to
    UW_ASSERT(policyClient->IsSatisfied(beginValSent));
  }
}

void Client2Client::ResetTrackingState() {
  std::unique_lock lock(sentFwdResultsMutex);
  beginValSent.clear();
  sentFwdResults.clear();
  valClientOrder.clear();
}

void Client2Client::SendForwardReadResultMessage(const std::string &key, const std::string &value, const Timestamp &ts,
    const proto::CommittedProof &proof, const std::string &serializedWrite, const std::string &serializedWriteTypeName, 
    const proto::Dependency &dep, bool hasDep, bool addReadset, const proto::Dependency &policyDep, bool hasPolicyDep) {

  if (!params.sintr_params.c2cSendThread) {
    SendForwardReadResultMessageHelper(key, value, ts, proof, serializedWrite, serializedWriteTypeName,
      dep, hasDep, addReadset, policyDep, hasPolicyDep);
  }
  else {
    auto f = [=]() {
      this->SendForwardReadResultMessageHelper(
        key, value, ts, proof, serializedWrite, 
        serializedWriteTypeName, dep, hasDep, addReadset,
        policyDep, hasPolicyDep
      );
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

void Client2Client::SendForwardReadResultMessageHelper(const std::string &key, const std::string &value, const Timestamp &ts,
    const proto::CommittedProof &proof, const std::string &serializedWrite, const std::string &serializedWriteTypeName, 
    const proto::Dependency &dep, bool hasDep, bool addReadset, const proto::Dependency &policyDep, bool hasPolicyDep) {

  SentFwdResultState *sentFwdResultState = new SentFwdResultState();
  proto::ForwardReadResultMessage *fwdReadResultMsgToSend = new proto::ForwardReadResultMessage();
  proto::ForwardReadResult fwdReadResult;
  fwdReadResult.set_key(key);
  fwdReadResult.set_value(value);
  if(!params.sintr_params.hideTimestamps) {
    fwdReadResult.mutable_timestamp()->set_timestamp(ts.getTimestamp());
    fwdReadResult.mutable_timestamp()->set_id(ts.getID());
  } else {
    std::string tsDigest = TimestampDigest(ts);
    fwdReadResult.set_hashed_timestamp(tsDigest);
  }
  fwdReadResult.set_client_id(client_id);
  fwdReadResult.set_client_seq_num(client_seq_num);
  fwdReadResult.set_add_readset(addReadset);

  // only if addReadset is true did this result come from server
  // otherwise it came from the buffer and there is no dependency or committed proof
  if (addReadset) {
    // this will contain the prepared txn dependency
    if (hasDep) {
      UW_ASSERT(dep.IsInitialized());
      *fwdReadResult.mutable_dep() = std::move(dep);
      // must be oneof write or signed write
      *fwdReadResultMsgToSend->mutable_write() = proto::Write();
    }
    else {
      if (params.validateProofs) {
        if (proof.IsInitialized()) {
          *fwdReadResultMsgToSend->mutable_proof() = std::move(proof);
        }
        // if no proof then it is possible the value is empty
        else {
          UW_ASSERT(value.length() == 0);
        }
      }

      // depending on if signatures are enabled and if the value is non empty
      if (serializedWriteTypeName == fwdReadResultMsgToSend->signed_write().GetTypeName()) {
        UW_ASSERT(fwdReadResultMsgToSend->mutable_signed_write()->ParseFromString(serializedWrite));
      }
      else if (serializedWriteTypeName == fwdReadResultMsgToSend->write().GetTypeName()) {
        UW_ASSERT(fwdReadResultMsgToSend->mutable_write()->ParseFromString(serializedWrite));
      }
      else {
        // this should only happen if value is empty
        UW_ASSERT(value.length() == 0);
        *fwdReadResultMsgToSend->mutable_write() = proto::Write();
      }
    }

    // separately include policy change txn dependency if there is one
    if (hasPolicyDep) {
      UW_ASSERT(policyDep.IsInitialized());
      *fwdReadResult.mutable_policy_dep() = std::move(policyDep);
    }
  }

  // copy into sentFwdResultState
  sentFwdResultState->fwdReadResult = fwdReadResult;

  if (params.sintr_params.signFwdReadResults) {
    
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
    CreateHMACedMessage(
      fwdReadResult,
      *fwdReadResultMsgToSend->mutable_signed_fwd_read_result(),
      beginValSent
    );
    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // create_hmac_us.add(duration);
  }
  else {
    *fwdReadResultMsgToSend->mutable_fwd_read_result() = std::move(fwdReadResult);
  }

  std::unique_lock lock(sentFwdResultsMutex);
  sentFwdResultState->fwdReadResultMsg = fwdReadResultMsgToSend;
  sentFwdResults.insert(sentFwdResultState);

  Debug(
    "ForwardReadResult: client id %lu, seq num %lu, key %s, value %s",
    client_id,
    client_seq_num,
    BytesToHex(key, 16).c_str(),
    BytesToHex(value, 16).c_str()
  );

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // send_fwd_read_time_us = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;

  for (const auto &i : beginValSent) {
    // do not send to self
    if (i == client_id) {
      continue;
    }
    transport->SendMessageToReplica(this, i, *fwdReadResultMsgToSend);
  }
}

void Client2Client::SendForwardPointQueryResultMessage(const std::string &key, const std::string &value, const Timestamp &ts,
    const std::string &table_name, const proto::CommittedProof &proof,
    const std::string &serializedWrite, const std::string &serializedWriteTypeName,
    const proto::Dependency &dep, bool hasDep, bool addReadset) {
  
  if (!params.sintr_params.c2cSendThread) {
    SendForwardPointQueryResultMessageHelper(
      key, value, ts, table_name, proof, serializedWrite, 
      serializedWriteTypeName, dep, hasDep, addReadset
    );
  }
  else {
    auto f = [=]() {
      this->SendForwardPointQueryResultMessageHelper(
        key, value, ts, table_name, proof, serializedWrite, 
        serializedWriteTypeName, dep, hasDep, addReadset
      );
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

// basically same logic as SendForwardReadResultMessageHelper
// no policy dep but additional table_name field
void Client2Client::SendForwardPointQueryResultMessageHelper(const std::string &key, const std::string &value, const Timestamp &ts,
    const std::string &table_name, const proto::CommittedProof &proof,
    const std::string &serializedWrite, const std::string &serializedWriteTypeName,
    const proto::Dependency &dep, bool hasDep, bool addReadset) {
  
  SentFwdResultState *sentFwdResultState = new SentFwdResultState();
  proto::ForwardPointQueryResultMessage *fwdPointQueryResultMsgToSend = new proto::ForwardPointQueryResultMessage();
  proto::ForwardReadResult fwdReadResult;
  fwdReadResult.set_key(key);
  fwdReadResult.set_value(value);
  if(!params.sintr_params.hideTimestamps) {
    fwdReadResult.mutable_timestamp()->set_timestamp(ts.getTimestamp());
    fwdReadResult.mutable_timestamp()->set_id(ts.getID());
  } else {
    std::string tsDigest = TimestampDigest(ts);
    fwdReadResult.set_hashed_timestamp(tsDigest);
  }
  fwdReadResult.set_client_id(client_id);
  fwdReadResult.set_client_seq_num(client_seq_num);
  fwdReadResult.set_table_name(table_name);
  fwdReadResult.set_add_readset(addReadset);

  // only if addReadset is true did this result come from server
  // otherwise it came from the buffer and there is no dependency or committed proof
  if (addReadset) {
    // this will contain the prepared txn dependency
    if (hasDep) {
      UW_ASSERT(dep.IsInitialized());
      *fwdReadResult.mutable_dep() = std::move(dep);
      // must be oneof write or signed write
      *fwdPointQueryResultMsgToSend->mutable_write() = proto::Write();
    }
    else {
      if (params.validateProofs) {
        if (proof.IsInitialized()) {
          *fwdPointQueryResultMsgToSend->mutable_proof() = std::move(proof);
        }
        // if no proof then it is possible the value is empty
        else {
          UW_ASSERT(value.length() == 0);
        }
      }

      // depending on if signatures are enabled and if the value is non empty
      if (serializedWriteTypeName == fwdPointQueryResultMsgToSend->signed_write().GetTypeName()) {
        UW_ASSERT(fwdPointQueryResultMsgToSend->mutable_signed_write()->ParseFromString(serializedWrite));
      }
      else if (serializedWriteTypeName == fwdPointQueryResultMsgToSend->write().GetTypeName()) {
        UW_ASSERT(fwdPointQueryResultMsgToSend->mutable_write()->ParseFromString(serializedWrite));
      }
      else {
        // this should only happen if value is empty
        UW_ASSERT(value.length() == 0);
        *fwdPointQueryResultMsgToSend->mutable_write() = proto::Write();
      }
    }
  }

  // copy into sentFwdResultState
  sentFwdResultState->fwdReadResult = fwdReadResult;

  if (params.sintr_params.signFwdReadResults) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
    CreateHMACedMessage(
      fwdReadResult,
      *fwdPointQueryResultMsgToSend->mutable_signed_fwd_read_result(),
      beginValSent
    );
    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // create_hmac_us.add(duration);
  }
  else {
    *fwdPointQueryResultMsgToSend->mutable_fwd_read_result() = std::move(fwdReadResult);
  }

  std::unique_lock lock(sentFwdResultsMutex);
  sentFwdResultState->fwdPointQueryResultMsg = fwdPointQueryResultMsgToSend;
  sentFwdResults.insert(sentFwdResultState);

  Debug(
    "ForwardPointQueryResult: client id %lu, seq num %lu, key %s, result %s",
    client_id,
    client_seq_num,
    key.c_str(),
    BytesToHex(value, 16).c_str()
  );

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // send_fwd_point_query_time_us = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    
  for (const auto &i : beginValSent) {
    // do not send to self
    if (i == client_id) {
      continue;
    }
    transport->SendMessageToReplica(this, i, *fwdPointQueryResultMsgToSend);
  }
}

void Client2Client::SendForwardQueryResultMessage(const std::string &query_gen_id, const std::string &query_result,
    const proto::QueryResultMetaData &query_res_meta,
    std::map<uint64_t, std::vector<proto::SignedMessage *>> *group_sigs, bool addReadset) {
  UW_ASSERT(group_sigs != nullptr);
  if (!params.sintr_params.c2cSendThread) {
    SendForwardQueryResultMessageHelper(
      query_gen_id, query_result,
      query_res_meta, *group_sigs, addReadset
    );
    delete group_sigs;
    // the signatures inside of group_sigs are moved in SendForwardQueryResultMessageHelper

    // query_res_meta is a newly allocated object only if result is not from cache (addReadset=true)
    // and cacheReadSet=false and mergeActiveAtClient=true
    if (addReadset && !params.query_params.cacheReadSet && params.query_params.mergeActiveAtClient) {
      delete &query_res_meta;
    }
  }
  else {
    std::function<void*(void)> f;
    if (!params.query_params.cacheReadSet && params.query_params.mergeActiveAtClient) {
      // here query_res_meta is ours to own so capture it by pointer
      f = [=, query_res_meta_ptr = &query_res_meta]() {
        this->SendForwardQueryResultMessageHelper(
          query_gen_id, query_result,
          *query_res_meta_ptr, *group_sigs, addReadset
        );
        delete group_sigs;
        delete query_res_meta_ptr;
        return (void*) true;
      };
    }
    else {
      f = [=]() {
        // here query_res_meta is from the client txn object, so we need to copy capture it
        this->SendForwardQueryResultMessageHelper(
          query_gen_id, query_result,
          query_res_meta, *group_sigs, addReadset
        );
        delete group_sigs;
        return (void*) true;
      };
    }
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

void Client2Client::SendForwardQueryResultMessageHelper(const std::string &query_gen_id, const std::string &query_result,
    const proto::QueryResultMetaData &query_res_meta,
    std::map<uint64_t, std::vector<proto::SignedMessage *>> &group_sigs, bool addReadset) {

  SentFwdResultState *sentFwdResultState = new SentFwdResultState();
  proto::ForwardQueryResultMessage *fwdQueryResultMsgToSend = new proto::ForwardQueryResultMessage();
  proto::ForwardQueryResult fwdQueryResult;
  fwdQueryResult.set_query_gen_id(query_gen_id);
  fwdQueryResult.set_query_result(query_result);
  fwdQueryResult.set_client_id(client_id);
  fwdQueryResult.set_client_seq_num(client_seq_num);
  fwdQueryResult.set_add_readset(addReadset);
  if(query_res_meta.IsInitialized()) {
    *fwdQueryResult.mutable_query_res_meta() = query_res_meta;
    if(params.sintr_params.hideTimestamps && !params.query_params.cacheReadSet) {
      // assuming we send over the readset only if cacheReadSet is false
      for (auto &[group, queryMeta] : *fwdQueryResult.mutable_query_res_meta()->mutable_group_meta()) {
        for (auto &read : *queryMeta.mutable_query_read_set()->mutable_read_set()) {
          read.clear_readtime();
        }
        for (auto &pred: *queryMeta.mutable_query_read_set()->mutable_read_predicates()){
          pred.clear_table_version();
        }
      }
    }
  }

  // copy into sentFwdResultState
  sentFwdResultState->fwdQueryResult = fwdQueryResult;
  
  if (params.sintr_params.signFwdReadResults) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
    CreateHMACedMessage(
      fwdQueryResult,
      *fwdQueryResultMsgToSend->mutable_signed_fwd_query_result(),
      beginValSent
    );
    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // create_hmac_us.add(duration);
  }
  else {
    *fwdQueryResultMsgToSend->mutable_fwd_query_result() = std::move(fwdQueryResult);
  }

  if (addReadset) {
    if (params.validateProofs) {
      for (auto &[group, query_sigs] : group_sigs) {
        proto::SignedMessages &curr_group_sigs = (*fwdQueryResultMsgToSend->mutable_query_sigs())[group];
        for (auto &query_sig : query_sigs) {
          *curr_group_sigs.add_sig_msgs() = std::move(*query_sig);
        }
      }
    }
  }

  std::unique_lock lock(sentFwdResultsMutex);
  sentFwdResultState->fwdQueryResultMsg = fwdQueryResultMsgToSend;
  sentFwdResults.insert(sentFwdResultState);

  Debug(
    "ForwardQueryResult: client id %lu, seq num %lu, query gen id %s add readset %d",
    client_id,
    client_seq_num,
    BytesToHex(query_gen_id, 16).c_str(),
    addReadset
  );
  for (const auto &i : beginValSent) {
    // do not send to self
    if (i == client_id) {
      continue;
    }
    transport->SendMessageToReplica(this, i, *fwdQueryResultMsgToSend);
  }
}

void Client2Client::SendBlindWriteMessage() {
  if (!params.sintr_params.c2cSendThread) {
    SendBlindWriteMessageHelper();
  }
  else {
    auto f = [=]() {
      this->SendBlindWriteMessageHelper();
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

void Client2Client::SendBlindWriteMessageHelper() {
  SentFwdResultState *sentFwdResultState = new SentFwdResultState();
  proto::BlindWriteMessage *blindWriteMsgToSend = new proto::BlindWriteMessage();
  proto::BlindWrite blindWrite;
  blindWrite.set_client_id(client_id);
  blindWrite.set_client_seq_num(client_seq_num);

  // copy into sentFwdResultState
  sentFwdResultState->blindWrite = blindWrite;

  if (params.sintr_params.signFwdReadResults) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
    CreateHMACedMessage(
      blindWrite,
      *blindWriteMsgToSend->mutable_signed_blind_write(),
      beginValSent
    );
    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // create_hmac_us.add(duration);
  }
  else {
    *blindWriteMsgToSend->mutable_blind_write() = std::move(blindWrite);
  }

  std::unique_lock lock(sentFwdResultsMutex);
  sentFwdResultState->blindWriteMsg = blindWriteMsgToSend;
  sentFwdResults.insert(sentFwdResultState);

  Debug(
    "SendBlindWrite: client id %lu, seq num %lu",
    client_id,
    client_seq_num
  );
  for (const auto &i : beginValSent) {
    // do not send to self
    if (i == client_id) {
      continue;
    }
    transport->SendMessageToReplica(this, i, *blindWriteMsgToSend);
  }
}

void Client2Client::HandlePolicyUpdate(const Policy *policy) {
  UW_ASSERT(policy != nullptr);
  endorseClient->UpdateRequirement(policy);

  if (!params.sintr_params.c2cSendThread) {
    HandlePolicyUpdateHelper(policy);
  }
  else {
    auto f = [=]() {
      this->HandlePolicyUpdateHelper(policy);
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

void Client2Client::HandlePolicyUpdateHelper(const Policy *policy) {
  std::vector<int> diff = endorseClient->DifferenceToSatisfied(beginValSent);
  // if after updating the policy, and the current set of validations is not enough, initiate more
  if (diff.size() > 0) {
    std::set<uint64_t> clients;
    ExtractFromPolicyClientsToContact(diff, clients);
    Debug("Initiating %ld more beginValTxnMsg", clients.size());
    std::shared_lock lock(sentFwdResultsMutex);
    for (const auto &i : clients) {
      // do not send to self
      if (i == client_id) {
        continue;
      }
      auto ret = beginValSent.insert(i);
      // should be first time sending to this client
      UW_ASSERT(ret.second);
      transport->SendMessageToReplica(this, i, sentBeginValTxnMsg);
      for (const auto &sentFwdResultState : sentFwdResults) {
        Debug(
          "Sending to client %lu from client %lu seq num %lu in handle policy update",
          i,
          client_id,
          client_seq_num
        );

        if (sentFwdResultState->fwdReadResultMsg != nullptr) {
          // need to HMAC again since previous one did not include this client
          if (params.sintr_params.signFwdReadResults && !sentFwdResultState->reHMACed) {
            CreateHMACedMessage(
              sentFwdResultState->fwdReadResult,
              *sentFwdResultState->fwdReadResultMsg->mutable_signed_fwd_read_result()
            );
          }
          transport->SendMessageToReplica(this, i, *sentFwdResultState->fwdReadResultMsg);
        }
        else if (sentFwdResultState->fwdPointQueryResultMsg != nullptr) {
          if (params.sintr_params.signFwdReadResults && !sentFwdResultState->reHMACed) {
            CreateHMACedMessage(
              sentFwdResultState->fwdReadResult,
              *sentFwdResultState->fwdPointQueryResultMsg->mutable_signed_fwd_read_result()
            );
          }
          transport->SendMessageToReplica(this, i, *sentFwdResultState->fwdPointQueryResultMsg);
        }
        else if (sentFwdResultState->fwdQueryResultMsg != nullptr) {
          if (params.sintr_params.signFwdReadResults && !sentFwdResultState->reHMACed) {
            CreateHMACedMessage(
              sentFwdResultState->fwdQueryResult,
              *sentFwdResultState->fwdQueryResultMsg->mutable_signed_fwd_query_result()
            );
          }
          transport->SendMessageToReplica(this, i, *sentFwdResultState->fwdQueryResultMsg);
        }
        else if (sentFwdResultState->blindWriteMsg != nullptr) {
          if (params.sintr_params.signFwdReadResults && !sentFwdResultState->reHMACed) {
            CreateHMACedMessage(
              sentFwdResultState->blindWrite,
              *sentFwdResultState->blindWriteMsg->mutable_signed_blind_write()
            );
          }
          transport->SendMessageToReplica(this, i, *sentFwdResultState->blindWriteMsg);
        }
        else {
          Panic("No non-nullptr message to send");
        }
        sentFwdResultState->reHMACed = true;
      }
    }
  }
}

void Client2Client::ManageDispatchBeginValidateTxnMessage(const TransportAddress &remote, const std::string &data) {
  if (!params.sintr_params.c2cReceiveThread) {
    beginValTxnMsg.ParseFromString(data);
    HandleBeginValidateTxnMessage(remote, beginValTxnMsg);
  }
  else {
    proto::BeginValidateTxnMessage *beginValTxnMsg = new proto::BeginValidateTxnMessage();
    beginValTxnMsg->ParseFromString(data);
    auto f = [this, &remote, beginValTxnMsg](){
      this->HandleBeginValidateTxnMessage(remote, *beginValTxnMsg);
      delete beginValTxnMsg;
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cReceiveQueue.push(executor);
  }
}

void Client2Client::ManageDispatchForwardReadResultMessage(const TransportAddress &remote, const std::string &data) {
  if (!params.sintr_params.c2cReceiveThread) {
    fwdReadResultMsg.ParseFromString(data);
    HandleForwardReadResultMessage(fwdReadResultMsg);
  }
  else {
    proto::ForwardReadResultMessage *fwdReadResultMsg = new proto::ForwardReadResultMessage();
    fwdReadResultMsg->ParseFromString(data);
    auto f = [this, fwdReadResultMsg](){
      this->HandleForwardReadResultMessage(*fwdReadResultMsg);
      delete fwdReadResultMsg;
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cReceiveQueue.push(executor);
  }
}

void Client2Client::ManageDispatchForwardPointQueryResultMessage(const TransportAddress &remote, const std::string &data) {
  if (!params.sintr_params.c2cReceiveThread) {
    fwdPointQueryResultMsg.ParseFromString(data);
    HandleForwardPointQueryResultMessage(fwdPointQueryResultMsg);
  }
  else {
    proto::ForwardPointQueryResultMessage *fwdPointQueryResultMsg = new proto::ForwardPointQueryResultMessage();
    fwdPointQueryResultMsg->ParseFromString(data);
    auto f = [this, fwdPointQueryResultMsg](){
      this->HandleForwardPointQueryResultMessage(*fwdPointQueryResultMsg);
      delete fwdPointQueryResultMsg;
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cReceiveQueue.push(executor);
  }
}

void Client2Client::ManageDispatchForwardQueryResultMessage(const TransportAddress &remote, const std::string &data) {
  if (!params.sintr_params.c2cReceiveThread) {
    fwdQueryResultMsg.ParseFromString(data);
    HandleForwardQueryResultMessage(fwdQueryResultMsg);
  }
  else {
    proto::ForwardQueryResultMessage *fwdQueryResultMsg = new proto::ForwardQueryResultMessage();
    fwdQueryResultMsg->ParseFromString(data);
    auto f = [this, fwdQueryResultMsg](){
      this->HandleForwardQueryResultMessage(*fwdQueryResultMsg);
      delete fwdQueryResultMsg;
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cReceiveQueue.push(executor);
  }
}

void Client2Client::ManageDispatchBlindWriteMessage(const TransportAddress &remote, const std::string &data) {
  if (!params.sintr_params.c2cReceiveThread) {
    blindWriteMsg.ParseFromString(data);
    HandleBlindWriteMessage(blindWriteMsg);
  }
  else {
    proto::BlindWriteMessage *blindWriteMsg = new proto::BlindWriteMessage();
    blindWriteMsg->ParseFromString(data);
    auto f = [this, blindWriteMsg](){
      this->HandleBlindWriteMessage(*blindWriteMsg);
      delete blindWriteMsg;
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cReceiveQueue.push(executor);
  }
}

void Client2Client::ManageDispatchFinishValidateTxnMessage(const TransportAddress &remote, const std::string &data) {
  if (!params.sintr_params.c2cReceiveThread && !params.sintr_params.parallelEndorsementCheck) {
    finishValTxnMsg.ParseFromString(data);

    if (params.sintr_params.optimisticReceiveEndorsement) {
      HandleFinishValidateTxnMessageOptimistic(finishValTxnMsg);
    }

    HandleFinishValidateTxnMessage(finishValTxnMsg);
  }
  else {
    proto::FinishValidateTxnMessage *finishValTxnMsg = new proto::FinishValidateTxnMessage();
    finishValTxnMsg->ParseFromString(data);
    auto f = [this, finishValTxnMsg](){
      this->HandleFinishValidateTxnMessage(*finishValTxnMsg);
      delete finishValTxnMsg;
      return (void*) true;
    };

    if (params.sintr_params.optimisticReceiveEndorsement) {
      HandleFinishValidateTxnMessageOptimistic(*finishValTxnMsg);
    }

    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    if (params.sintr_params.parallelEndorsementCheck) {
      // fully parallelize the endorsement check so that each one can be handled by a worker thread
      parallelSigCheckQueue.push(executor);
    }
    else {
      // only moves the function to be off the main client thread, but still sequential on client2client message thread
      c2cReceiveQueue.push(executor);
    }
  }
}

void Client2Client::HandleBeginValidateTxnMessage(const TransportAddress &remote, 
    const proto::BeginValidateTxnMessage &beginValTxnMsg) {
  // if (verify_hmac_us.count > 0 && verify_hmac_us.count % 2000 == 0) {
  //   std::cerr << "Mean verify HMAC latency: " << verify_hmac_us.mean() << std::endl;
  //   std::cerr << "Mean check committed prepared latency: " << check_committed_prepared_us.mean() << std::endl;
  //   std::cerr << "Mean send finish validation latency: " << send_finish_val_us.mean() << std::endl;
  // }
  // if (validation_time_us.count > 0 && validation_time_us.count % 2000 == 0) {
  //   std::cerr << "Mean validation queue latency: " << validation_queue_time_us.mean() << std::endl;
  //   std::cerr << "Mean validation time: " << validation_time_us.mean() << std::endl;
  // }

  proto::BeginValidateTxn beginValTxn;
  if (params.sintr_params.signFwdReadResults) {
    if (!beginValTxnMsg.has_signed_begin_validate_txn()) {
      Debug("Missing client signature on begin validate txn message");
      return;
    }

    std::string data;
    if (!ValidateHMACedMessage(beginValTxnMsg.signed_begin_validate_txn(), data)) {
      Debug("Invalid client signature on begin validate txn message");
      return;
    }

    beginValTxn.ParseFromString(data);
  }
  else {
    beginValTxn = beginValTxnMsg.begin_validate_txn();
  }

  uint64_t curr_client_id = beginValTxn.client_id();
  uint64_t curr_client_seq_num = beginValTxn.client_seq_num();
  TxnState txnState = beginValTxn.txn_state();
  Timestamp ts;
  std::string hashed_ts = "";
  if(params.sintr_params.hideTimestamps) {
    hashed_ts = beginValTxn.hashed_ts();
    Debug("hashed TS validation: %s", BytesToHex(hashed_ts, 16).c_str());
  } else {
    ts = beginValTxn.timestamp();
  }
  Debug(
    "HandleBeginValidateTxnMessage: from client id %lu, seq num %lu", 
    curr_client_id, 
    curr_client_seq_num
  );
  ValidationTransaction *valTxn = valParseClient->Parse(txnState);
  TransportAddress *remoteCopy = remote.clone();
  ValidationInfo *valInfo = new ValidationInfo(curr_client_id, curr_client_seq_num, ts, std::move(valTxn), std::move(remoteCopy), hashed_ts);
  valInfo->isPolicyTransaction = (txnState.txn_name().find("policy") != std::string::npos);
  validationQueue.push(valInfo);

  // ping.set_salt(curr_client_id);
  // MySendPing(curr_client_id, ping, false);
}

void Client2Client::HandleForwardReadResultMessage(const proto::ForwardReadResultMessage &fwdReadResultMsg) {
  proto::ForwardReadResult fwdReadResult;
  if (params.sintr_params.signFwdReadResults) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    // first check client signature
    // Debugs will not include client ID/client seq num because they are included in the fwdReadResult
    if (!fwdReadResultMsg.has_signed_fwd_read_result()) {
      Debug("Missing client signature on forwarded read result");
      return;
    }
    std::string data;
    if (!ValidateHMACedMessage(fwdReadResultMsg.signed_fwd_read_result(), data)) {
      Debug("Invalid client signature on forwarded read result");
      return;
    }

    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // verify_hmac_us.add(duration);

    fwdReadResult.ParseFromString(data);
  }
  else {
    fwdReadResult = fwdReadResultMsg.fwd_read_result();
  }

  uint64_t curr_client_id = fwdReadResult.client_id();
  uint64_t curr_client_seq_num = fwdReadResult.client_seq_num();

  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();

  proto::Write write;
  bool hasDep = fwdReadResult.has_dep();
  proto::Dependency dep;
  bool addReadset = fwdReadResult.add_readset();
  // only if addReadset is true will there be dep or committed proofs
  if (addReadset && params.sintr_params.clientCheckEvidence) {
    if (!CheckPreparedCommittedEvidence(fwdReadResult, fwdReadResultMsg, write, dep)) {
      return;
    }
    // if there is an actual value, expect matches
    if (curr_value.length() > 0) {
      UW_ASSERT(write.key() == curr_key);
      if (hasDep) {
        UW_ASSERT(write.prepared_value() == curr_value);
        if(params.sintr_params.hideTimestamps) {
          UW_ASSERT(write.hashed_prepared_ts() == fwdReadResult.hashed_timestamp());
        } else {
          UW_ASSERT(google::protobuf::util::MessageDifferencer::Equals(write.prepared_timestamp(), fwdReadResult.timestamp()));
        }
      }
      else {
        UW_ASSERT(write.committed_value() == curr_value);
        if(params.sintr_params.hideTimestamps) {
          UW_ASSERT(write.hashed_committed_ts() == fwdReadResult.hashed_timestamp());
        } else {
          UW_ASSERT(google::protobuf::util::MessageDifferencer::Equals(write.committed_timestamp(), fwdReadResult.timestamp()));
        }
      }
    }
    // otherwise the write should be empty
    else {
      UW_ASSERT(!write.has_key());
    }

    // curr_key is essentially what the forwarding client is claiming is the key
    // write contains the server's claim as to what the key is
    // these two should match
    // also if value is empty, then no need to check since server makes no claims about it
    if (curr_value.length() > 0 && curr_key != write.key()) {
      Debug(
        "Mismatch in forwarded key and the server key: from client id %lu, seq num %lu, forwarded key %s, server key %s",
        curr_client_id, 
        curr_client_seq_num,
        BytesToHex(curr_key, 16).c_str(),
        BytesToHex(write.key(), 16).c_str()
      );
      return;
    }
  }

  bool hasPolicyDep = fwdReadResult.has_policy_dep();
  proto::Dependency policyDep;
  if (hasPolicyDep) {
    policyDep = fwdReadResult.policy_dep();
  }

  Debug(
    "HandleForwardReadResult: from client id %lu, seq num %lu, key %s, value %s", 
    curr_client_id, 
    curr_client_seq_num,
    BytesToHex(curr_key, 16).c_str(),
    BytesToHex(curr_value, 16).c_str()
  );
  // tell valClient about this forwardedReadResult
  valClient->ProcessForwardReadResult(
    curr_client_id, curr_client_seq_num, fwdReadResult,
    dep, hasDep, addReadset, policyDep, hasPolicyDep
  );
}

void Client2Client::HandleForwardPointQueryResultMessage(const proto::ForwardPointQueryResultMessage &fwdPointQueryResultMsg) {
  proto::ForwardReadResult fwdReadResult;
  if (params.sintr_params.signFwdReadResults) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    // first check client signature
    // Debugs will not include client ID/client seq num because they are included in the fwdReadResult
    if (!fwdPointQueryResultMsg.has_signed_fwd_read_result()) {
      Debug("Missing client signature on forwarded read result");
      return;
    }
    std::string data;
    if (!ValidateHMACedMessage(fwdPointQueryResultMsg.signed_fwd_read_result(), data)) {
      Debug("Invalid client signature on forwarded read result");
      return;
    }

    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // verify_hmac_us.add(duration);

    fwdReadResult.ParseFromString(data);
  }
  else {
    fwdReadResult = fwdPointQueryResultMsg.fwd_read_result();
  }

  uint64_t curr_client_id = fwdReadResult.client_id();
  uint64_t curr_client_seq_num = fwdReadResult.client_seq_num();

  std::string curr_key = fwdReadResult.key();
  std::string curr_value = fwdReadResult.value();

  proto::Write write;
  bool hasDep = fwdReadResult.has_dep();
  proto::Dependency dep;
  bool addReadset = fwdReadResult.add_readset();
  // only if addReadset is true will there be dep or committed proofs
  if (addReadset && params.sintr_params.clientCheckEvidence) {
    if (!CheckPreparedCommittedEvidence(fwdReadResult, fwdPointQueryResultMsg, write, dep)) {
      Panic("Invalid prepared or committed evidence on forwarded point query result");
      return;
    }
    // point query dependencies don't contain full information about the write, only txn digest
    // so we can't check the key 
    if (!hasDep) {
      // if there is an actual value, expect matches
      if (curr_value.length() > 0) {
        UW_ASSERT(write.key() == curr_key);
        UW_ASSERT(write.committed_value() == curr_value);
        if(params.sintr_params.hideTimestamps) {
          UW_ASSERT(write.hashed_committed_ts() == fwdReadResult.hashed_timestamp());
        } else {
          UW_ASSERT(google::protobuf::util::MessageDifferencer::Equals(write.committed_timestamp(), fwdReadResult.timestamp()));
        }
      }
      // otherwise the write should be empty
      else {
        UW_ASSERT(!write.has_key());
      }

      // curr_key is essentially what the forwarding client is claiming is the key
      // write contains the server's claim as to what the key is
      // these two should match
      // also if value is empty, then no need to check since server makes no claims about it
      if (curr_value.length() > 0 && curr_key != write.key()) {
        Debug(
          "Mismatch in forwarded key and the server key: from client id %lu, seq num %lu, forwarded key %s, server key %s",
          curr_client_id, 
          curr_client_seq_num,
          BytesToHex(curr_key, 16).c_str(),
          BytesToHex(write.key(), 16).c_str()
        );
        return;
      }
    }
  }

  Debug(
    "HandleForwardPointQueryResult: from client id %lu, seq num %lu, key %s, value %s", 
    curr_client_id, 
    curr_client_seq_num,
    curr_key.c_str(),
    BytesToHex(curr_value, 16).c_str()
  );
  // tell valClient about this forwardedReadResult
  valClient->ProcessForwardPointQueryResult(
    curr_client_id, curr_client_seq_num, fwdReadResult,
    dep, hasDep, addReadset
  );
}

void Client2Client::HandleForwardQueryResultMessage(const proto::ForwardQueryResultMessage &fwdQueryResultMsg) {

  proto::ForwardQueryResult fwdQueryResult;
  if (params.sintr_params.signFwdReadResults) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    // first check client signature
    // Debugs will not include client ID/client seq num because they are included in the fwdQueryResult
    if (!fwdQueryResultMsg.has_signed_fwd_query_result()) {
      Debug("Missing client signature on forwarded query result");
      return;
    }
    std::string data;
    if (!ValidateHMACedMessage(fwdQueryResultMsg.signed_fwd_query_result(), data)) {
      Debug("Invalid client signature on forwarded query result");
      return;
    }

    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // verify_hmac_us.add(duration);

    fwdQueryResult.ParseFromString(data);
  }
  else {
    fwdQueryResult = fwdQueryResultMsg.fwd_query_result();
  }

  uint64_t curr_client_id = fwdQueryResult.client_id();
  uint64_t curr_client_seq_num = fwdQueryResult.client_seq_num();

  std::string curr_query_gen_id = fwdQueryResult.query_gen_id();
  std::string curr_query_result = fwdQueryResult.query_result();

  bool addReadset = fwdQueryResult.add_readset();
  if (addReadset && params.sintr_params.clientCheckEvidence) {
    if (!params.sintr_params.parallelQuerySigsCheck) {
      if (!CheckPreparedCommittedEvidence(fwdQueryResult, fwdQueryResultMsg)) {
        Panic("Invalid prepared or committed evidence on forwarded query result");
        return;
      }
    }
    else {
      Debug("HandleForwardQueryResult parallel query sig check: from client id %lu, seq num %lu, query gen id %s, query result %s",
        curr_client_id, 
        curr_client_seq_num,
        BytesToHex(curr_query_gen_id, 16).c_str(),
        BytesToHex(curr_query_result, 16).c_str()
      );
      // this will be async so no need to check the result or tell valClient
      CheckPreparedCommittedEvidence(fwdQueryResult, fwdQueryResultMsg);
      return;
    }
  }

  Debug(
    "HandleForwardQueryResult: from client id %lu, seq num %lu, query gen id %s, query result %s", 
    curr_client_id, 
    curr_client_seq_num,
    BytesToHex(curr_query_gen_id, 16).c_str(),
    BytesToHex(curr_query_result, 16).c_str()
  );
  // tell valClient about this forwardedReadResult
  valClient->ProcessForwardQueryResult(curr_client_id, curr_client_seq_num, fwdQueryResult, addReadset);
}

void Client2Client::HandleBlindWriteMessage(const proto::BlindWriteMessage &blindWriteMsg) {
  proto::BlindWrite blindWrite;
  if (params.sintr_params.signFwdReadResults) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    // first check client signature
    if (!blindWriteMsg.has_signed_blind_write()) {
      Debug("Missing client signature on blind write");
      return;
    }
    std::string data;
    if (!ValidateHMACedMessage(blindWriteMsg.signed_blind_write(), data)) {
      Debug("Invalid client signature on blind write");
      return;
    }

    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // verify_hmac_us.add(duration);

    blindWrite.ParseFromString(data);
  }
  else {
    blindWrite = blindWriteMsg.blind_write();
  }

  uint64_t curr_client_id = blindWrite.client_id();
  uint64_t curr_client_seq_num = blindWrite.client_seq_num();
  Debug(
    "HandleBlindWrite: from client id %lu, seq num %lu", 
    curr_client_id, 
    curr_client_seq_num
  );
  // tell valClient about this blindWrite
  valClient->ProcessBlindWrite(curr_client_id, curr_client_seq_num);
}

void Client2Client::HandleFinishValidateTxnMessage(const proto::FinishValidateTxnMessage &finishValTxnMsg) {
  // struct timespec ts_start;
  // clock_gettime(CLOCK_MONOTONIC, &ts_start);
  // uint64_t finish = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
  // auto duration = finish - send_begin_time_us;
  // send_begin_to_receive_endorse_us.add(duration);
  // auto duration = finish - send_fwd_read_time_us;
  // fwd_read_to_receive_endorse_us.add(duration);
  // auto duration = finish - send_fwd_point_query_time_us;
  // fwd_point_query_to_receive_endorse_us.add(duration);
  // size_t numEndorsementsReceived = endorseClient->GetEndorsements().size();
  // if (numEndorsementsReceived + 1 > time_to_endorse_n_us.size()) {
  //   time_to_endorse_n_us.resize(numEndorsementsReceived + 1);
  // }
  // time_to_endorse_n_us[numEndorsementsReceived].add(duration);

  uint64_t peer_client_id = finishValTxnMsg.client_id();
  uint64_t val_txn_seq_num = finishValTxnMsg.validation_txn_seq_num();

  // client_time_to_endorse_us[peer_client_id].add(duration);

  // stale finish validation message
  if (val_txn_seq_num != client_seq_num) {
    Debug(
      "Received stale finishValidateTxnMessage from client id %lu, seq num %lu; curr seq num %lu", 
      peer_client_id, 
      val_txn_seq_num,
      client_seq_num
    );
    return;
  }

  proto::SignedMessage signedMsg;
  std::string valTxnDigest;
  if (params.sintr_params.signFinishValidation) {
    // verify signature
    if (!finishValTxnMsg.has_signed_validation_txn_digest()) {
      Debug("Missing signed validation txn digest sent from client id %lu", peer_client_id);
      return;
    }
    signedMsg = finishValTxnMsg.signed_validation_txn_digest();

    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    if(!clients_verifier->Verify(keyManager->GetPublicKey(keyManager->GetClientKeyId(signedMsg.process_id())),
        signedMsg.data(), signedMsg.signature())) {
      Debug("Invalid signature on validation txn digest sent from client id %lu", peer_client_id);
      return;
    }
    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // verify_endorse_us.add(duration);

    valTxnDigest = signedMsg.data();
  }
  else {
    // dummy signed message
    signedMsg.set_process_id(peer_client_id);
    signedMsg.set_data(finishValTxnMsg.validation_txn_digest());
    signedMsg.set_signature("");
    valTxnDigest = finishValTxnMsg.validation_txn_digest();
  }

  Debug("HandleFinishValidateTxnMessage: txn digest %s for client %lu from client %lu with seq number %lu",
    BytesToHex(valTxnDigest, 16).c_str(), client_id, peer_client_id,val_txn_seq_num);

  if (params.sintr_params.debugEndorseCheck) {
    endorseClient->DebugCheck(finishValTxnMsg.val_txn());
  }

  if (!params.sintr_params.optimisticReceiveEndorsement) {
    endorseClient->AddValidation(peer_client_id, valTxnDigest, signedMsg);
  }
  // in optimistic case, endorsement is added outside so just check
  else {
    endorseClient->CheckValidation(peer_client_id, val_txn_seq_num, valTxnDigest);
  }
}

void Client2Client::HandleFinishValidateTxnMessageOptimistic(const proto::FinishValidateTxnMessage &finishValTxnMsg) {
  uint64_t peer_client_id = finishValTxnMsg.client_id();
  uint64_t val_txn_seq_num = finishValTxnMsg.validation_txn_seq_num();
  // stale finish validation message
  if (val_txn_seq_num != client_seq_num) {
    Debug(
      "Received stale finishValidateTxnMessage from client id %lu, seq num %lu; curr seq num %lu", 
      peer_client_id, 
      val_txn_seq_num,
      client_seq_num
    );
    return;
  }

  if (params.sintr_params.signFinishValidation) {
    UW_ASSERT(finishValTxnMsg.has_signed_validation_txn_digest());
    endorseClient->AddValidationOptimistic(
      peer_client_id,
      finishValTxnMsg.signed_validation_txn_digest()
    );
  }
  else {
    // dummy signed message
    proto::SignedMessage signedMsg;
    signedMsg.set_process_id(peer_client_id);
    signedMsg.set_data(finishValTxnMsg.validation_txn_digest());
    signedMsg.set_signature("");
    endorseClient->AddValidationOptimistic(
      peer_client_id,
      signedMsg
    );
  }
}

bool Client2Client::CheckPreparedCommittedEvidence(const proto::ForwardReadResult &fwdReadResult, 
  const proto::ForwardReadResultMessage &fwdReadResultMsg, proto::Write &write, proto::Dependency &dep) {
  // struct timespec ts_start;
  // clock_gettime(CLOCK_MONOTONIC, &ts_start);
  // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

  uint64_t curr_client_id = fwdReadResult.client_id();
  uint64_t curr_client_seq_num = fwdReadResult.client_seq_num();

  // if has dependency, then this is based on a prepared txn
  if (fwdReadResult.has_dep()) {
    if (params.validateProofs && params.signedMessages && params.verifyDeps) {
      if (!ValidateDependency(fwdReadResult.dep(), config, params.readDepSize, 
          keyManager, verifier)) {
        Debug(
          "Invalid dependency on forwarded read result from client id %lu, seq num %lu",
          curr_client_id, 
          curr_client_seq_num
        );
        return false;
      }
    }
    dep = fwdReadResult.dep();
    write = fwdReadResult.dep().write();
  } 
  else {
    // otherwise can check committed proof and signature

    if (params.validateProofs && params.signedMessages) {
      // check server signature
      if (fwdReadResultMsg.has_signed_write()) {
        if (!verifier->Verify(keyManager->GetPublicKey(fwdReadResultMsg.signed_write().process_id()),
            fwdReadResultMsg.signed_write().data(), fwdReadResultMsg.signed_write().signature())) {
          Debug(
            "Invalid server signature on forwarded read result from client id %lu, seq num %lu", 
            curr_client_id, 
            curr_client_seq_num
          );
          return false;
        }

        write.ParseFromString(fwdReadResultMsg.signed_write().data());
      }
      else {
        if (fwdReadResultMsg.has_write() && fwdReadResultMsg.write().has_committed_value()) {
          Debug(
            "Missing server signature on forwarded read result with committed value from client id %lu, seq num %lu", 
            curr_client_id, 
            curr_client_seq_num
          );
          return false;
        }

        write = fwdReadResultMsg.write();
      }
    }
    else {
      write = fwdReadResultMsg.write();
    }
      
    if (params.validateProofs) {
      // check committed proof
      if (write.has_committed_value() && (write.has_committed_timestamp() || write.has_hashed_committed_ts())) {
        if (!fwdReadResultMsg.has_proof()) {
          Debug(
            "Missing committed value proof for forwarded read result from client id %lu, seq num %lu",
            curr_client_id,
            curr_client_seq_num
          );
          return false;
        }

        std::string committedTxnDigest = TransactionDigest(fwdReadResultMsg.proof().txn(), params.hashDigest, params.sintr_params.hideTimestamps);
        if(params.sintr_params.hashEndorsements) {
          committedTxnDigest = EndorsedTxnDigest(committedTxnDigest, fwdReadResultMsg.proof().txn(), params.hashDigest);
        }
        if (!ValidateTransactionWrite(fwdReadResultMsg.proof(), &committedTxnDigest,
            write.key(), write.committed_value(), write.has_committed_timestamp() ? write.committed_timestamp() : Timestamp(),
            config, params.signedMessages, keyManager, verifier, write.has_hashed_committed_ts() ? write.hashed_committed_ts() : "")) {
          Debug(
            "Failed to validate committed value for forwarded read result from client id %lu, seq num %lu",
            curr_client_id,
            curr_client_seq_num
          );
          return false;
        }
      }
    }
  }

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
  // auto duration = end - start;
  // check_committed_prepared_us.add(duration);

  return true;
}

bool Client2Client::CheckPreparedCommittedEvidence(const proto::ForwardReadResult &fwdPointQueryResult, 
  const proto::ForwardPointQueryResultMessage &fwdPointQueryResultMsg, proto::Write &write, proto::Dependency &dep) {
  // struct timespec ts_start;
  // clock_gettime(CLOCK_MONOTONIC, &ts_start);
  // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

  uint64_t curr_client_id = fwdPointQueryResult.client_id();
  uint64_t curr_client_seq_num = fwdPointQueryResult.client_seq_num();

  // if has dependency, then this is based on a prepared txn
  if (fwdPointQueryResult.has_dep()) {
    if (params.validateProofs && params.signedMessages && params.verifyDeps) {
      if (!ValidateDependency(fwdPointQueryResult.dep(), config, params.readDepSize, 
          keyManager, verifier)) {
        Debug(
          "Invalid dependency on forwarded point query result from client id %lu, seq num %lu",
          curr_client_id, 
          curr_client_seq_num
        );
        return false;
      }
    }
    dep = fwdPointQueryResult.dep();
    write = fwdPointQueryResult.dep().write();
  }
  else {
    // otherwise can check committed proof and signature

    if (params.validateProofs && params.signedMessages) {
      // check server signature
      if (fwdPointQueryResultMsg.has_signed_write()) {
        if (!verifier->Verify(keyManager->GetPublicKey(fwdPointQueryResultMsg.signed_write().process_id()),
            fwdPointQueryResultMsg.signed_write().data(), fwdPointQueryResultMsg.signed_write().signature())) {
          Debug(
            "Invalid server signature on forwarded point query result from client id %lu, seq num %lu", 
            curr_client_id, 
            curr_client_seq_num
          );
          return false;
        }

        write.ParseFromString(fwdPointQueryResultMsg.signed_write().data());
      }
      else {
        if (fwdPointQueryResultMsg.has_write() && fwdPointQueryResultMsg.write().has_committed_value()) {
          Debug(
            "Missing server signature on forwarded read result with committed value from client id %lu, seq num %lu", 
            curr_client_id, 
            curr_client_seq_num
          );
          return false;
        }

        write = fwdPointQueryResultMsg.write();
      }
    }
    else {
      write = fwdPointQueryResultMsg.write();
    }
      
    if (params.validateProofs) {
      // check committed proof
      if (write.has_committed_value() && (write.has_committed_timestamp() || write.has_hashed_committed_ts())) {
        if (!fwdPointQueryResultMsg.has_proof()) {
          Debug(
            "Missing committed value proof for forwarded point query result from client id %lu, seq num %lu",
            curr_client_id,
            curr_client_seq_num
          );
          return false;
        }

        std::string committedTxnDigest = TransactionDigest(fwdPointQueryResultMsg.proof().txn(), params.hashDigest, params.sintr_params.hideTimestamps);
        if(params.sintr_params.hashEndorsements && fwdPointQueryResultMsg.proof().txn().has_txndigest()) {
          committedTxnDigest = fwdPointQueryResultMsg.proof().txn().txndigest();
        } else if(params.sintr_params.hashEndorsements) {
          Debug("NO TXN DIGEST IN PROOF FOR CLIENT2CLIENT forward point query result");
        }

        sql::QueryResultProtoWrapper query_result;
        if (!ValidateTransactionTableWrite(fwdPointQueryResultMsg.proof(), &committedTxnDigest,
            write.has_committed_timestamp() ? write.committed_timestamp() : Timestamp(), write.key(), write.committed_value(),
            fwdPointQueryResult.table_name(), &query_result, sql_interpreter,
            config, params.signedMessages, keyManager, verifier, params.sintr_params.hideTimestamps, true)) {
          Debug(
            "Failed to validate committed value for forwarded point query result from client id %lu, seq num %lu",
            curr_client_id,
            curr_client_seq_num
          );
          return false;
        }
      }
    }
  }

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
  // auto duration = end - start;
  // check_committed_prepared_us.add(duration);

  return true;
}

bool Client2Client::CheckPreparedCommittedEvidence(const proto::ForwardQueryResult &fwdQueryResult,
    const proto::ForwardQueryResultMessage &fwdQueryResultMsg) {

  // struct timespec ts_start;
  // clock_gettime(CLOCK_MONOTONIC, &ts_start);
  // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

  uint64_t curr_client_id = fwdQueryResult.client_id();
  uint64_t curr_client_seq_num = fwdQueryResult.client_seq_num();
  const std::string &query_gen_id = fwdQueryResult.query_gen_id();
  const std::string &query_result = fwdQueryResult.query_result();

  uint64_t num_matches = 0;
  if (params.validateProofs && params.signedMessages) {
    std::shared_ptr<AsyncQuerySigCheck> asyncQuerySigCheck = std::make_shared<AsyncQuerySigCheck>(params.query_params.resultQuorum);

    for (const auto &[group, curr_query_sigs] : fwdQueryResultMsg.query_sigs()) {
      uint64_t total_sigs = curr_query_sigs.sig_msgs_size();
      for (const auto &query_sig : curr_query_sigs.sig_msgs()) {
        const proto::ReadSet &query_read_set = fwdQueryResult.query_res_meta().group_meta().at(group).query_read_set();
        const std::string &query_read_set_hash = fwdQueryResult.query_res_meta().group_meta().at(group).read_set_hash();
        if (!params.sintr_params.parallelQuerySigsCheck) {
          if (!CheckQuerySigHelper(query_sig, query_gen_id, query_result,
              query_read_set, query_read_set_hash)) {
            Debug(
              "Invalid query signature on forwarded query result from client id %lu, seq num %lu",
              curr_client_id,
              curr_client_seq_num
            );
            continue;
          }
  
          num_matches++;
        }
        else {
          // send each query sig to worker thread
          auto f = [
            this, asyncQuerySigCheck, total_sigs,
            curr_client_id, curr_client_seq_num,
            fwdQueryResult, addReadset=fwdQueryResult.add_readset(),
            query_sig, query_gen_id, query_result,
            query_read_set, query_read_set_hash
          ] {
            bool is_valid = this->CheckQuerySigHelper(query_sig, query_gen_id, query_result, query_read_set, query_read_set_hash);
            std::lock_guard<std::mutex> lock(asyncQuerySigCheck->mtx);

            Debug(
              "Async query sig check for client id %lu, seq num %lu, query gen id %s, is valid %d",
              curr_client_id,
              curr_client_seq_num,
              BytesToHex(query_gen_id, 16).c_str(),
              is_valid
            );

            if (is_valid) {
              ++asyncQuerySigCheck->num_check_passed;
            }
            ++asyncQuerySigCheck->num_finished;

            if (asyncQuerySigCheck->called_val_client) {
              // if valClient has already been called, then just return
              return (void*) true;
            }

            if (asyncQuerySigCheck->num_check_passed >= params.query_params.resultQuorum) {
              asyncQuerySigCheck->called_val_client = true;
              valClient->ProcessForwardQueryResult(curr_client_id, curr_client_seq_num, fwdQueryResult, addReadset);
              return (void*) true;
            }
            // all sigs have been checked, but not enough matches
            else if (asyncQuerySigCheck->num_finished == total_sigs) {
              Panic(
                "Insufficient number of matches for forwarded query result from client id %lu, seq num %lu",
                curr_client_id,
                curr_client_seq_num
              );
              return (void*) false;
            }

            return (void*) true;
          };
          Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
          parallelSigCheckQueue.push(executor);
        }
      }
    }

    if (!params.sintr_params.parallelQuerySigsCheck) {
      if (num_matches < params.query_params.resultQuorum) {
        Debug(
          "Insufficient number of matches for forwarded query result from client id %lu, seq num %lu", 
          curr_client_id, 
          curr_client_seq_num
        );
        return false;
      }
    }
  }

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
  // auto duration = end - start;
  // check_committed_prepared_us.add(duration);

  return true;
}

bool Client2Client::CheckQuerySigHelper(const proto::SignedMessage &query_sig,
    const std::string &query_gen_id, const std::string &query_result,
    const proto::ReadSet &query_read_set, const std::string &query_read_set_hash) {
  
  proto::QueryResult validated_result;
  // first check signature
  if (!verifier->Verify(keyManager->GetPublicKey(query_sig.process_id()),
      query_sig.data(), query_sig.signature())) {
    Debug("Invalid server signature on forwarded query result");
    return false;
  }

  if (!validated_result.ParseFromString(query_sig.data())) {
    Debug("Failed to parse query result");
    return false;
  }

  // next make sure that we have matches
  if (validated_result.query_gen_id() != query_gen_id) {
    Debug("Mismatch in query gen id for forwarded query result");
    return false;
  }

  if (validated_result.query_result() != query_result) {
    Debug("Mismatch in query result for forwarded query result");
    return false;
  }

  // check readset
  if (params.query_params.cacheReadSet) {
    // only expect hash
    if (validated_result.query_result_hash() != query_read_set_hash) {
      Debug("Mismatch in read set hash for forwarded query result");
      return false;
    }
  }
  else {
    // expect full readset
    // compute hash to compare

    // validated_result query read set is from the signed message so could be unsorted
    try {
      std::sort(validated_result.mutable_query_read_set()->mutable_read_set()->begin(), validated_result.mutable_query_read_set()->mutable_read_set()->end(), sortReadSetByKey);
      //erase duplicates: Technically not necessary.
      validated_result.mutable_query_read_set()->mutable_read_set()->erase(std::unique(validated_result.mutable_query_read_set()->mutable_read_set()->begin(),
          validated_result.mutable_query_read_set()->mutable_read_set()->end(), equalReadMsg), validated_result.mutable_query_read_set()->mutable_read_set()->end());  //erases all but last appearance
      //Note: Only necessary because we use repeated field; Not necessary if we used ordered map
    }
    catch(...) {
      Panic("Read set contains two reads of the same key with different timestamp. Sent by replica %d", validated_result.replica_id());
    }
    std::string validated_result_hash = generateReadSetSingleHash(validated_result.query_read_set(), params.sintr_params.hideTimestamps);
    std::string fwd_read_set_hash = generateReadSetSingleHash(query_read_set, params.sintr_params.hideTimestamps);
    if (validated_result_hash != fwd_read_set_hash) {
      Debug("Mismatch in read set for forwarded query result");
      return false;
    }
  }

  return true;
}

void Client2Client::ExtractFromPolicyClientsToContact(const std::vector<int> &policySatSet, std::set<uint64_t> &clients) {
  const std::set<uint64_t> &blacklistedClients = endorseClient->GetBlacklistedClients();
  
  int offset = 1;
  size_t order_index = 0;
  for (const auto &i : policySatSet) {
    if (i == client_id) {
      continue;
    }
    // i < 0 means can choose any client
    else if (i < 0) {
      if (valClientSelector != nullptr) {
        for (; order_index < valClientOrder.size(); order_index++) {
          uint64_t target = valClientOrder[order_index];
          if (blacklistedClients.find(target) != blacklistedClients.end()) {
            continue;
          }
          if (beginValSent.find(target) == beginValSent.end() && clients.find(target) == clients.end()) {
            clients.insert(target);
            break;
          }
        }
        if (order_index == valClientOrder.size()) {
          Panic("Policy requires more endorsements than available clients");
        }
      }
      // if no selector, then use offset for ring style selection
      else {
        for (; offset < clients_config->n; offset++) {
          uint64_t target = (client_id + offset) % clients_config->n;
          if (blacklistedClients.find(target) != blacklistedClients.end()) {
            continue;
          }
          if (beginValSent.find(target) == beginValSent.end() && clients.find(target) == clients.end()) {
            clients.insert(target);
            break;
          }
        }
        // if we reach the end of the loop, then we have exhausted all clients
        if (offset == clients_config->n) {
          Panic("Policy requires more endorsements than available clients");
        }
      }
    }
    // otherwise i is a specific client to contact
    else {
      if (blacklistedClients.find(i) != blacklistedClients.end()) {
        Panic("Client %lu is blacklisted but is in policySatSet", i);
      }

      if (beginValSent.find(i) == beginValSent.end()) {
        clients.insert(i);
      }
      else {
        Panic("Client %lu already sent beginValTxnMsg to client %d", client_id, i);
      }
    }
  }
}

void Client2Client::ValidationThreadFunction() {
  ::SyncClient syncClient(valClient);

  if(params.query_params.sql_mode) {
    valClient->SetThreadValSQLInterpreter();
  }

  while(!done) {
    ValidationInfo *valInfo;
    validationQueue.pop(valInfo);
    if (valInfo == nullptr) {
      continue;
    }
    // struct timespec ts_startstart;
    // clock_gettime(CLOCK_MONOTONIC, &ts_startstart);
    // uint64_t startstart = ts_startstart.tv_sec * 1000 * 1000 + ts_startstart.tv_nsec / 1000;
    // auto dur = startstart - valInfo->start_time_us;
    // validation_queue_time_us.add(dur);
    
    uint64_t curr_client_id = valInfo->txn_client_id;
    uint64_t curr_client_seq_num = valInfo->txn_client_seq_num;
    Timestamp curr_ts = valInfo->txn_ts;
    ValidationTransaction *valTxn = valInfo->valTxn;

    std::ostringstream oss;
    oss << std::this_thread::get_id();
    Debug(
      "%s will validate for client id %lu, seq num %lu",
      oss.str().c_str(),
      curr_client_id,
      curr_client_seq_num
    );

    valClient->SetThreadValTxnId(curr_client_id, curr_client_seq_num);
    valClient->SetTxnTimestamp(curr_client_id, curr_client_seq_num, curr_ts, valInfo->isPolicyTransaction, valInfo->hashed_ts);

    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    transaction_status_t result = valTxn->Validate(syncClient);

    struct timespec ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    auto duration = end - start;
    // validation_time_us.add(duration);

    if (result == COMMITTED) {
      // struct timespec ts_start;
      // clock_gettime(CLOCK_MONOTONIC, &ts_start);
      // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

      Debug("Completed validation for client id %lu, seq num %lu", curr_client_id, curr_client_seq_num);
      proto::Transaction *txn = valClient->GetCompletedTxn(curr_client_id, curr_client_seq_num);

      if (params.parallel_CCC) {
        std::sort(txn->mutable_read_set()->begin(), txn->mutable_read_set()->end(), sortReadSetByKey);
        std::sort(txn->mutable_write_set()->begin(), txn->mutable_write_set()->end(), sortWriteSetByKey);
        if (params.sintr_params.sortWriteset && params.query_params.sql_mode && !valInfo->isPolicyTransaction) {
          AddWriteSetIdx(*txn);
          // also sort row updates
          for (auto &[table, table_write]: *txn->mutable_table_writes()) {
            std::sort(table_write.mutable_rows()->begin(), table_write.mutable_rows()->end(), sortRowUpdates);
          }
          AddRowUpdatesIdx(*txn);
        }
      }
      else if (params.sintr_params.sortWriteset && params.query_params.sql_mode && !valInfo->isPolicyTransaction) {
        // must sort writeset always, because validation client writeset ordering is not guaranteed in query mode
        std::sort(txn->mutable_write_set()->begin(), txn->mutable_write_set()->end(), sortWriteSetByKey);
        AddWriteSetIdx(*txn);
        // also sort row updates
        for (auto &[table, table_write]: *txn->mutable_table_writes()) {
          std::sort(table_write.mutable_rows()->begin(), table_write.mutable_rows()->end(), sortRowUpdates);
        }
        AddRowUpdatesIdx(*txn);
      }

      std::sort(txn->mutable_involved_groups()->begin(), txn->mutable_involved_groups()->end());

      proto::FinishValidateTxnMessage finishValTxnMsg;
      finishValTxnMsg.set_client_id(client_id);
      finishValTxnMsg.set_validation_txn_seq_num(curr_client_seq_num);

      // only send over digest, not actual contents
      // if hide timestamps is true, then hash timestamps
      std::string digest = TransactionDigest(*txn, params.hashDigest, params.sintr_params.hideTimestamps);
      Debug("Validation Digest is : %s", BytesToHex(digest, 16).c_str());
      if (params.sintr_params.signFinishValidation) {
        // sign the digest
        SignBytes(
          digest, 
          keyManager->GetPrivateKey(keyManager->GetClientKeyId(client_id)), 
          client_id, 
          finishValTxnMsg.mutable_signed_validation_txn_digest()
        );
      }
      else {
        finishValTxnMsg.set_validation_txn_digest(digest);
      }

      if (params.sintr_params.debugEndorseCheck) {
        *finishValTxnMsg.mutable_val_txn() = *txn;
      }

      // struct timespec ts_end;
      // clock_gettime(CLOCK_MONOTONIC, &ts_end);
      // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
      // auto duration = end - start;
      // send_finish_val_us.add(duration);

      transport->SendMessage(this, *valInfo->remote, finishValTxnMsg);
      delete txn;
    }

    delete valInfo;
    Debug("thread exiting for validation for client id %lu, seq num %lu", curr_client_id, curr_client_seq_num);
  }
  Debug("done true, exiting validation thread");
}

void Client2Client::Client2ClientExecutorThreadFunction(tbb::concurrent_bounded_queue<Client2ClientExecutor *> &c2cQueue) {
  while (!done) {
    Client2ClientExecutor *executor;
    c2cQueue.pop(executor);
    if (executor == nullptr) {
      continue;
    }
    executor->f();
    delete executor;
  }
}

bool Client2Client::ValidateHMACedMessage(const proto::SignedMessage &signedMessage, std::string &data) {
  data = signedMessage.data();
  proto::HMACs hmacs;
  hmacs.ParseFromString(signedMessage.signature());
  return crypto::verifyHMAC(
    signedMessage.data(), 
    (*hmacs.mutable_hmacs())[client_id], 
    sessionKeys[signedMessage.process_id() % clients_config->n]
  );
}

void Client2Client::CreateHMACedMessage(const ::google::protobuf::Message &msg, proto::SignedMessage& signedMessage) {
  std::set<uint64_t> dst_client_ids;
  for (uint64_t i = 0; i < clients_config->n; i++) {
    dst_client_ids.insert(i);
  }
  CreateHMACedMessage(msg, signedMessage, dst_client_ids);
}

void Client2Client::CreateHMACedMessage(const ::google::protobuf::Message &msg, proto::SignedMessage& signedMessage,
    const std::set<uint64_t> &dst_client_ids) {
  std::string msgData = msg.SerializeAsString();
  signedMessage.set_data(msgData);
  signedMessage.set_process_id(client_id);
  proto::HMACs hmacs;
  for (uint64_t i : dst_client_ids) {
    if (i == client_id) {
      // no need to sign for self
      continue;
    }
    (*hmacs.mutable_hmacs())[i] = crypto::HMAC(msgData, sessionKeys[i]);
  }
  signedMessage.set_signature(hmacs.SerializeAsString());
}

} // namespace sintrstore
