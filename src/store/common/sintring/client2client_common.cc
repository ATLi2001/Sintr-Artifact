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
#include "lib/message.h"
#include <sstream>
#include <sched.h>
#include <pthread.h>

Client2ClientCommon::Client2ClientCommon(uint64_t client_id, transport::Configuration *clients_config, Transport *transport,
    int group, SintrParameters sintr_params) :
    client_id(client_id), clients_config(clients_config), transport(transport),
    group(group), sintr_params(sintr_params), done(false) {

  transport->Register(this, *clients_config, group, client_id);

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

  // each process gets 2 cpus, one for main client thread and one for all validation, send, receive, sig check threads 
  int num_cpus = std::thread::hardware_concurrency();
  size_t cpus_per_client = 2;
  // if we give more sig check threads, up to 4 cpus per client
  if (sintr_params.maxClientSigCheckThreads > 0) {
    cpus_per_client = 4;
  }
  int main_client_cpu = (client_id * cpus_per_client) % num_cpus;

  // derived Client2Client classes should instantiate the validation threads
  // Debug("Starting %lu validation threads", sintr_params.maxValThreads);

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
}

void Client2ClientCommon::ValidationThreadFunction(ValidationClientCommon *valClient,
    std::function<void(void)> preValFunc, std::function<void(transaction_status_t)> postValFunc) {
  ::SyncClient syncClient(valClient);

  while(!done) {
    ValidationInfoBase *valInfo;
    validationQueue.pop(valInfo);
    if (valInfo == nullptr) {
      continue;
    }
    
    uint64_t curr_client_id = valInfo->txn_client_id;
    uint64_t curr_client_seq_num = valInfo->txn_client_seq_num;
    // Timestamp curr_ts = valInfo->txn_ts;
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
    // valClient->SetTxnTimestamp(curr_client_id, curr_client_seq_num, curr_ts, valInfo->isPolicyTransaction, valInfo->hashed_ts);

    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    transaction_status_t result = valTxn->Validate(syncClient);

    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // validation_time_us.add(duration);

    postValFunc(result);

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