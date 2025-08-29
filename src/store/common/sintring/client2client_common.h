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

#ifndef _CLIENT2CLIENT_COMMON_H_
#define _CLIENT2CLIENT_COMMON_H_

#include "lib/transport.h"
#include "store/common/frontend/validation_transaction.h"
#include "store/common/sintring/params.h"
#include "store/common/sintring/validation_client_common.h"
#include "store/common/sintring/endorsement_client.h"
#include "store/common/sintring/validation_parse_client.h"
#include "store/common/policy/client_selector.h"
#include "store/common/policy/policy_client.h"
#include "store/common/policy/policy.h"
#include "tbb/concurrent_queue.h"
#include <google/protobuf/message.h>
#include <atomic>
#include <vector>
#include <string>
#include <random>

class ValidationInfoBase;

// this class provides common functionality for client-to-client sintr protocol
// eventually, should modify some of the messages such as BeginValidateTxnMessage to be common as well
class Client2ClientCommon : public TransportReceiver {
public:

  Client2ClientCommon(uint64_t client_id, transport::Configuration *clients_config, Transport *transport, int group, SintrParameters sintr_params,
    EndorsementClient *endorseClient, ClientSelector *valClientSelector, std::mt19937 &rand,
    const std::vector<std::string> &keys = std::vector<std::string>());
  virtual ~Client2ClientCommon();

  // Init() for calling pure virtual functions, which should not be called in constructor
  void Init();

  // given a new policy, update the endorsement policy for this client 
  // also contact additional peers as necessary
  void HandlePolicyUpdate(const Policy *policy);

  // for sending/receiving messages from other clients
  struct Client2ClientExecutor {
    Client2ClientExecutor(std::function<void*(void)> f) : f(std::move(f)) {}
    std::function<void*(void)> f;
  };

protected:
  void ResetTrackingState();

  virtual const ::google::protobuf::Message *GetSentBeginValTxnMsg() const = 0;
  void HandlePolicyUpdateHelper(const Policy *policy);

  // create an hmac from msg and place into signature
  // creates hmac for every client
  virtual void CreateHMACedMessage(const ::google::protobuf::Message &msg,
    ::google::protobuf::Message &signedMessage, const std::string &signedTypeName) = 0;

  // return a set of client ids to contact for validation based on the heuristic in sintr_params
  std::set<uint64_t> ProcessClientValidationHeuristic(PolicyClient *policyClient);
  // extract client ids not currently in beginValSent from policy satisfying set
  void ExtractFromPolicyClientsToContact(const std::vector<int> &policySatSet, std::set<uint64_t> &clients);

  virtual void ValidationThreadFunction() = 0;
  void ValidationThreadFunctionBase(ValidationClientCommon *valClient,
    std::function<void(void)> preValFunc, std::function<void(transaction_status_t, ValidationInfoBase*)> postValFunc);
  void Client2ClientExecutorThreadFunction(tbb::concurrent_bounded_queue<Client2ClientExecutor *> &c2cQueue);

  // this represents a resizing buffer but does not eagerly delete everything on clear
  // instead it will delete buffer elements as they are replaced
  template <typename T>
  struct LazyBuffer {
    LazyBuffer() : size(0) {}
    ~LazyBuffer() {
      for (auto &b : buffer) {
        delete b;
      }
      buffer.clear();
    }
    void insert(T *t) {
      if (size < buffer.size()) {
        delete buffer[size];
        buffer[size] = t;
      }
      else {
        buffer.push_back(t);
      }
      size++;
    }
    T *operator[](size_t i) {
      if (i >= size) {
        Panic("LazyBuffer index out of bounds");
      }
      return buffer[i];
    }
    size_t getSize() {
      return size;
    }
    void clear() {
      size = 0;
    }
    // iterators
    typename std::vector<T *>::iterator begin() {
      return buffer.begin();
    }
    typename std::vector<T *>::iterator end() {
      return buffer.begin() + size;
    }

    std::vector<T *> buffer;
    size_t size;
  };

  struct SentFwdResultState {
    SentFwdResultState() : fwdMsgUnderlying(nullptr), fwdMsgSigned(nullptr) {}
    ~SentFwdResultState() {
      if (fwdMsgUnderlying != nullptr) {
        delete fwdMsgUnderlying;
      }
      if (fwdMsgSigned != nullptr) {
        delete fwdMsgSigned;
      }
    }

    ::google::protobuf::Message *fwdMsgUnderlying;
    ::google::protobuf::Message *fwdMsgSigned;
    std::string signedTypeName;
    // because on initial send, we only hmac for the clients we send to
    // on sending to other clients, we need to re-hmac
    // after re-hmac for all clients, we set this to true
    bool reHMACed = false;
  };

  const uint64_t client_id;
  transport::Configuration *clients_config;
  Transport *transport;
  const int group;
  SintrParameters sintr_params;
  // endorsement client can inform client of received validations
  EndorsementClient *endorseClient;
  ClientSelector *valClientSelector;
  std::mt19937 &rand;
  // order of validation clients to contacts
  std::vector<uint64_t> valClientOrder;
  // for keySelector based benchmark validation, need copy of keys for validator as well
  const std::vector<std::string> &keys;

  // current set of transport ids begin validation message has been sent to
  std::set<uint64_t> beginValSent;
  ValidationParseClient *valParseClient;

  // for hmacs
  std::unordered_map<uint64_t, std::string> sessionKeys;

  // current transaction sequence number (to send to others)
  uint64_t client_seq_num;

  // track all sent forward read/query results for current transaction
  LazyBuffer<SentFwdResultState> sentFwdResults;
  mutable std::shared_mutex sentFwdResultsMutex;

  // threads for validation
  std::vector<std::thread *> valThreads;
  std::atomic<bool> done;
  // concurrent queue of transactions to be validated, has blocking semantics for pop
  tbb::concurrent_bounded_queue<ValidationInfoBase *> validationQueue;
  // separate thread for message sending, stays sequential
  std::thread *c2cSendThread;
  // concurrent queue of messages to be sent
  tbb::concurrent_bounded_queue<Client2ClientExecutor *> c2cSendQueue;
  // separate thread for message receiving, stays sequential
  std::thread *c2cReceiveThread;
  // concurrent queue of messages to be received
  tbb::concurrent_bounded_queue<Client2ClientExecutor *> c2cReceiveQueue;

  // for parallel signature checks
  std::vector<std::thread *> parallelSigCheckThreads;
  tbb::concurrent_bounded_queue<Client2ClientExecutor *> parallelSigCheckQueue;
};

class ValidationInfoBase {
public:
  ValidationInfoBase(uint64_t txn_client_id, uint64_t txn_client_seq_num, 
    ValidationTransaction *valTxn, TransportAddress *remote) : 
    txn_client_id(txn_client_id), txn_client_seq_num(txn_client_seq_num), 
    valTxn(valTxn), remote(remote) {};
  virtual ~ValidationInfoBase() {
    delete valTxn;
    delete remote;
  };

  // client id that initiated this validation
  uint64_t txn_client_id;
  // sequence number of transaction on initiating client
  uint64_t txn_client_seq_num;
  // actual transaction that we can call Validate on
  ValidationTransaction *valTxn;
  // address of initiating client
  TransportAddress *remote;
};



#endif /* _CLIENT2CLIENT_COMMON_H_ */
