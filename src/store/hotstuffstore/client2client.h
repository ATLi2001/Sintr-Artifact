// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
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

#ifndef _HOTSTUFFSTORE_CLIENT2CLIENT_H_
#define _HOTSTUFFSTORE_CLIENT2CLIENT_H_


#include "lib/keymanager.h"
#include "lib/assert.h"
#include "lib/configuration.h"
#include "lib/crypto.h"
#include "lib/message.h"
#include "lib/transport.h"
#include "store/common/timestamp.h"
#include "store/common/transaction.h"
#include "store/common/common-proto.pb.h"
#include "store/hotstuffstore/common.h"
#include "store/hotstuffstore/validation_client.h"
#include "store/common/sintring/validation_parse_client.h"
#include "store/common/sintring/endorsement_client.h"
#include "store/common/sintring/client2client_common.h"
#include "store/common/sintring/params.h"
#include "store/common/policy/policy.h"
#include "store/common/policy/client_selector.h"

#include <map>
#include <string>
#include <vector>
#include <set>
#include <atomic>
#include <shared_mutex>

#include "tbb/concurrent_queue.h"

namespace hotstuffstore {

typedef std::function<void()> endorsement_callback;

class Client2Client : public Client2ClientCommon {
 public:
  Client2Client(transport::Configuration *clients_config, Transport *transport,
      uint64_t client_id, uint64_t nshards, uint64_t ngroups, Partitioner *part, int group, bool signMessages, bool validateProofs,
      SintrParameters sintr_params, KeyManager *keyManager,
      EndorsementClient *endorseClient, ClientSelector *valClientSelector, std::mt19937 &rand,
      const std::vector<std::string> &keys = std::vector<std::string>());
  virtual ~Client2Client();

  virtual void ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data,
      void *meta_data) override;

  // start up the sintr validation for current transaction
  // sends BeginValidateTxnMessage to peers
  // takes ownership of policyClient, which contains the estimated policy for the transaction
  void SendBeginValidateTxnMessage(uint64_t client_seq_num, const TxnState &protoTxnState, uint64_t txnStartTime, PolicyClient *policyClient);
  
  // forward read results to other clients
  void SendForwardReadResultMessage(const std::string &key, const std::string &value,
    const proto::CommitProof &proof, const Timestamp &timestamp, const proto::SignedMessage &signedMsg);

  void SetFailureFlag(bool f) {
    failureActive = f;
  }
  void SetEndorsementCallback(endorsement_callback endorse_cb);

 private:
  virtual const ::google::protobuf::Message *GetSentBeginValTxnMsg() const override;

  void SendBeginValidateTxnMessageHelper(const uint64_t client_seq_num, const TxnState &protoTxnState, uint64_t txnStartTime,
    PolicyClient *policyClient);
  
  void SendForwardReadResultMessageHelper(const std::string &key, const std::string &value,
    const proto::CommitProof &proof, const Timestamp &timestamp, const proto::SignedMessage &signedMsg);

  void ManageDispatchBeginValidateTxnMessage(const TransportAddress &remote, const std::string &data);
  void ManageDispatchForwardReadResultMessage(const TransportAddress &remote, const std::string &data);
  void ManageDispatchFinishValidateTxnMessage(const TransportAddress &remote, const std::string &data);

  void HandleBeginValidateTxnMessage(const TransportAddress &remote, const proto::BeginValidateTxnMessage &beginValTxnMsg);
  void HandleForwardReadResultMessage(const proto::ForwardReadResultMessage &fwdReadResultMsg);
  void HandleFinishValidateTxnMessage(const proto::FinishValidateTxnMessage &finishValTxnMsg,
    std::shared_ptr<proto::SignedMessage> signedMsg);
  // optimistic does not check endorsement for validity, just accepts it
  // used before normal HandleFinishValidateTxnMessage
  void HandleFinishValidateTxnMessageOptimistic(const proto::FinishValidateTxnMessage &finishValTxnMsg,
    std::shared_ptr<proto::SignedMessage> signedMsg);

  // check if fwdQueryResult is valid based on f+1 matching server responses in fwdQueryResultMsg
  bool CheckPreparedCommittedEvidence(const proto::ForwardReadResult &fwdReadResult,
    const proto::ForwardReadResultMessage &fwdReadResultMsg);
  // helper for query result check evidence
  bool CheckReadSigHelper(const proto::SignedMessage &signedMessage, const proto::CommitProof &proof,
    const std::string &key, const std::string &value, const Timestamp &ts);

  bool ValidateReadProof(const proto::CommitProof& commitProof, const std::string& key,
    const std::string& value, const Timestamp& timestamp);

  virtual void ValidationThreadFunction() override;

  bool ValidateHMACedMessage(const proto::SignedMessage &signedMessage, std::string &data);

  virtual void CreateHMACedMessage(const ::google::protobuf::Message &msg,
    ::google::protobuf::Message &signedMessage, const std::string &signedTypeName) override;
  // create an hmac from msg and place into signature
  // creates hmac for every client
  void CreateHMACedMessage(const ::google::protobuf::Message &msg, proto::SignedMessage& signedMessage);
  // creates hmac for only dst_client_ids
  void CreateHMACedMessage(const ::google::protobuf::Message &msg, proto::SignedMessage& signedMessage,
    const std::set<uint64_t> &dst_client_ids);

  void SetEndorsementCallbackHelper(endorsement_callback endorse_cb);

  // Number of shards.
  uint64_t nshards;
  // Number of replica groups.
  uint64_t ngroups;

  bool signMessages;
  bool validateProofs;

  KeyManager *keyManager;
  bool failureActive;

  ValidationClient *valClient;

  // track most recently sent begin validation message
  proto::BeginValidateTxnMessage sentBeginValTxnMsg;

  // for received messages
  proto::BeginValidateTxnMessage beginValTxnMsg;
  proto::ForwardReadResultMessage fwdReadResultMsg;
  proto::FinishValidateTxnMessage finishValTxnMsg;
  endorsement_callback endorse_cb;
  mutable std::shared_mutex seq_num_lock;
};

// contains necessary information for ValidationClient to validate
class ValidationInfo : public ValidationInfoBase {
public:
  ValidationInfo(uint64_t txn_client_id, uint64_t txn_client_seq_num,
      ValidationTransaction *valTxn, TransportAddress *remote) : 
      ValidationInfoBase(txn_client_id, txn_client_seq_num, valTxn, remote) {
    struct timespec ts_start;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    start_time_us = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
  }
  virtual ~ValidationInfo() {}

private:
  // start time in microseconds
  uint64_t start_time_us;
};

} // namespace hotstuffstore

#endif /* _HOTSTUFFSTORE_CLIENT2CLIENT_H_ */