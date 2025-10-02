/***********************************************************************
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

#include "store/common/sintring/client2client_common.h"
#include "store/common/frontend/sync_client.h"
#include "lib/assert.h"
#include "lib/message.h"
#include <sstream>
#include <sched.h>
#include <pthread.h>

Client2ClientCommon::Client2ClientCommon(uint64_t client_id, transport::Configuration *clients_config, Transport *transport,
    int group, SintrParameters sintr_params, EndorsementClient *endorseClient, ClientSelector *valClientSelector, std::mt19937 &rand,
    const std::vector<std::string> &keys) :
    client_id(client_id), clients_config(clients_config), transport(transport),
    group(group), sintr_params(sintr_params), endorseClient(endorseClient), valClientSelector(valClientSelector), rand(rand),
    keys(keys), done(false) {

  valParseClient = new ValidationParseClient(10000, sintr_params.policyFunctionName, keys); // TODO: pass arg for timeout length
  transport->Register(this, *clients_config, group, client_id);
  if(sintr_params.maxClientsConnect > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }


  // for hmac between clients
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
}

Client2ClientCommon::~Client2ClientCommon() {
  done = true;
  // send a dummy message to unblock any waiting threads before joining
  for (size_t i = 0; i < valThreads.size(); i++) {
    validationQueue.push(nullptr);
  }
  for (auto t : valThreads) {
    t->join();
    delete t;
  }
  if (sintr_params.c2cSendThread) {
    c2cSendQueue.push(nullptr);
    c2cSendThread->join();
    delete c2cSendThread;
  }
  if (sintr_params.c2cReceiveThread) {
    c2cReceiveQueue.push(nullptr);
    c2cReceiveThread->join();
    delete c2cReceiveThread;
  }
  for (size_t i = 0; i < parallelSigCheckThreads.size(); i++) {
    parallelSigCheckQueue.push(nullptr);
  }
  for (auto t : parallelSigCheckThreads) {
    t->join();
    delete t;
  }
  delete valParseClient;
}

void Client2ClientCommon::Init() {
  // each process gets 2 cpus, one for main client thread and one for all validation, send, receive, sig check threads 
  int num_cpus = std::thread::hardware_concurrency();
  size_t cpus_per_client = 2;
  // if we give more sig check threads, up to 4 cpus per client
  if (sintr_params.maxClientSigCheckThreads > 0) {
    cpus_per_client = 4;
  }
  int main_client_cpu = (client_id * cpus_per_client) % num_cpus;

  // derived Client2Client classes should override ValidationThreadFunction
  Debug("Starting %lu validation threads", sintr_params.maxValThreads);
  for (size_t i = 0; i < sintr_params.maxValThreads; i++) {
    valThreads.push_back(new std::thread(&Client2ClientCommon::ValidationThreadFunction, this));
    if (sintr_params.clientPinCores) {
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + (1 + i) % cpus_per_client) % num_cpus, &cpuset);
      pthread_setaffinity_np(valThreads[i]->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }

  if (sintr_params.c2cSendThread) {
    Debug("Starting c2cSendThread");
    c2cSendThread = new std::thread(&Client2ClientCommon::Client2ClientExecutorThreadFunction, this, std::ref(c2cSendQueue));
    if (sintr_params.clientPinCores) {
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + 1) % num_cpus, &cpuset);
      pthread_setaffinity_np(c2cSendThread->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }
  if (sintr_params.c2cReceiveThread) {
    Debug("Starting c2cReceiveThread");
    c2cReceiveThread = new std::thread(&Client2ClientCommon::Client2ClientExecutorThreadFunction, this, std::ref(c2cReceiveQueue));
    if (sintr_params.clientPinCores) {
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + 1) % num_cpus, &cpuset);
      pthread_setaffinity_np(c2cReceiveThread->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }

  for (size_t i = 0; i < sintr_params.maxClientSigCheckThreads; i++) {
    parallelSigCheckThreads.push_back(
      new std::thread(&Client2ClientCommon::Client2ClientExecutorThreadFunction, this, std::ref(parallelSigCheckQueue))
    );
    if (sintr_params.clientPinCores) {
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + (2 + i) % cpus_per_client) % num_cpus, &cpuset);
      pthread_setaffinity_np(parallelSigCheckThreads[i]->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }
  if(sintr_params.separateTransport) {
    c2cTportThread = new std::thread(&Client2ClientCommon::Client2ClientRunTCPThreadFunction, this);
    if (sintr_params.clientPinCores) {
      // don't pin cores for transport thread yet... ask if necessary
      // set cpu affinity
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET((main_client_cpu + 1) % num_cpus, &cpuset);
      pthread_setaffinity_np(c2cTportThread->native_handle(), sizeof(cpu_set_t), &cpuset);
    }
  }
  UW_ASSERT(sintr_params.maxClientsConnect < clients_config->n);
  // total number of clients should always be more than the max amount of clients to contact
  if(sintr_params.maxClientsConnect) {
    sendPing.set_salt(client_id);
    replyPing.set_salt(client_id);
    sendPing.set_send_msg(true);
    replyPing.set_send_msg(false);
  }
  for(int i = 1; i <= sintr_params.maxClientsConnect; i++) {
    sendDone = false;
    replyDone = false;
    //TODO: Currently assumes selector is a ring selector
    uint64_t target = (client_id + i) % clients_config->n;
    uint64_t reply_to = (clients_config->n + client_id - i) % clients_config->n;
    Debug("Target: %lu Reply to: %lu", target, reply_to);
    Debug("Client %lu sending ping to client %lu", client_id, target);
    if(target != client_id) {
      Debug("PING SALT for target: %lu and is send true %d", sendPing.salt(), sendPing.send_msg());
      transport->SendMessageToReplica(this, target, sendPing);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    Debug("Client %lu sending another ping to client %lu", client_id, reply_to);
    if(reply_to != client_id && reply_to != target) {
      Debug("PING SALT for replyTo: %lu and is send true %d", replyPing.salt(), replyPing.send_msg());
      transport->SendMessageToReplica(this, reply_to, replyPing);
    }
    // need to wait for replies as well
    std::unique_lock lk(tcpMutex);
    if(!cvSend.wait_for(lk, std::chrono::seconds(5), [this]{return sendDone;})) {
      Panic("Timeout: Sent ping not responded to");
    }
    if(!cvReply.wait_for(lk, std::chrono::seconds(5), [this]{return replyDone;})) {
      Panic("Timeout: Reply ping not responded to");
    }
    lk.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  Debug("FINISHED SENDING AND RECEIVING PINGS");

}

void Client2ClientCommon::ResetTrackingState() {
  std::unique_lock lock(sentFwdResultsMutex);
  beginValSent.clear();
  sentFwdResults.clear();
  valClientOrder.clear();
}

void Client2ClientCommon::HandlePolicyUpdate(const Policy *policy) {
  UW_ASSERT(policy != nullptr);
  endorseClient->UpdateRequirement(policy);

  if (!sintr_params.c2cSendThread) {
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

void Client2ClientCommon::Client2ClientRunTCPThreadFunction() {
  transport->Run();
}

void Client2ClientCommon::HandlePolicyUpdateHelper(const Policy *policy) {
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
      transport->SendMessageToReplica(this, i, *GetSentBeginValTxnMsg());
      for (const auto &sentFwdResultState : sentFwdResults) {
        Debug(
          "Sending to client %lu from client %lu seq num %lu in handle policy update",
          i,
          client_id,
          client_seq_num
        );

        if (sentFwdResultState->fwdMsgSigned != nullptr) {
          // need to HMAC again since previous one did not include this client
          if (sintr_params.signFwdReadResults && !sentFwdResultState->reHMACed) {
            CreateHMACedMessage(
              *sentFwdResultState->fwdMsgUnderlying,
              *sentFwdResultState->fwdMsgSigned,
              sentFwdResultState->signedTypeName
            );
          }
          transport->SendMessageToReplica(this, i, *sentFwdResultState->fwdMsgSigned);
        }
        else {
          Panic("No non-nullptr message to send");
        }
        sentFwdResultState->reHMACed = true;
      }
    }
  }
}

std::set<uint64_t> Client2ClientCommon::ProcessClientValidationHeuristic(PolicyClient *policyClient) {
  // for tracking purposes, must have self
  beginValSent.insert(client_id);

  std::set<uint64_t> out;
  // send to all clients so no need to bother with estimated policy
  if (sintr_params.clientValidationHeuristic == CLIENT_VALIDATION_HEURISTIC::ALL) {
    for (int i = 0; i < clients_config->n; i++) {
      out.insert(i);
    }
  }
  // other heuristics depend on actual policy that was estimated
  else {
    // precompute the order of clients to contact
    if (valClientSelector != nullptr) {
      // false = without replacement
      valClientOrder = valClientSelector->GetClientIds(rand, clients_config->n, false);
    }

    // need to use DifferenceToSatisfied to account for self
    ExtractFromPolicyClientsToContact(policyClient->DifferenceToSatisfied(beginValSent), out);

    if (sintr_params.clientValidationHeuristic == CLIENT_VALIDATION_HEURISTIC::EXACT) {
    }
    else if (sintr_params.clientValidationHeuristic == CLIENT_VALIDATION_HEURISTIC::ONE_MORE) {
      for (int i = 0; i < clients_config->n; i++) {
        if (i != client_id && out.find(i) == out.end()) {
          out.insert(i);
        }
      }
    }
    else {
      Panic("Invalid clientValidationHeuristic value");
    }
  }

  return out;
}

void Client2ClientCommon::ExtractFromPolicyClientsToContact(const std::vector<int> &policySatSet, std::set<uint64_t> &clients) {
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
        Panic("Client %d is blacklisted but is in policySatSet", i);
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

void Client2ClientCommon::ValidationThreadFunctionBase(ValidationClientCommon *valClient,
    std::function<void(void)> preValFunc, std::function<void(transaction_status_t, ValidationInfoBase*)> postValFunc) {
  ::SyncClient syncClient(valClient);

  while(!done) {
    ValidationInfoBase *valInfo;
    validationQueue.pop(valInfo);
    if (valInfo == nullptr) {
      continue;
    }
    
    uint64_t curr_client_id = valInfo->txn_client_id;
    uint64_t curr_client_seq_num = valInfo->txn_client_seq_num;
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
    preValFunc();

    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
    transaction_status_t result;
    try {
      result = valTxn->Validate(syncClient);
    } catch (const std::exception& e) {
      // std::cerr << "Caught an exception: " << e.what() << std::endl;
      Notice("catch abort for validated txn for client %lu : %lu.", curr_client_id, curr_client_seq_num);
      result = ABORTED_SYSTEM; //ABORTED_USER;
    }


    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // validation_time_us.add(duration);

    postValFunc(result, valInfo);

    delete valInfo;
    Debug("thread exiting for validation for client id %lu, seq num %lu", curr_client_id, curr_client_seq_num);
  }
  Debug("done true, exiting validation thread");
}

void Client2ClientCommon::Client2ClientExecutorThreadFunction(tbb::concurrent_bounded_queue<Client2ClientExecutor *> &c2cQueue) {
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

void Client2ClientCommon::HandlePingMessage(const PingMessage &ping) {
  if (ping.salt() != client_id) {
    Debug("Sending ping from client %lu to client %lu", client_id, ping.salt());
    transport->SendMessageToReplica(this, ping.salt(), ping);
  }
  else {
    Debug("Received own ping");
    if(ping.send_msg()) {
      if(sintr_params.maxClientsConnect > 0) {
        Debug("Received own ping for send");
        sendDone = true;
        cvSend.notify_one();
      }
    } else {
      if(sintr_params.maxClientsConnect > 0) {
        Debug("Received own ping for receive");
        replyDone = true;
        cvReply.notify_one();
      }
    }
  }

}