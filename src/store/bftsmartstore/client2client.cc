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

#include "store/bftsmartstore/client2client.h"
#include "store/bftsmartstore/validation_client.h"
#include "store/bftsmartstore/pbft_batched_sigs.h"
#include "store/common/frontend/validation_transaction.h"
#include "store/common/util.h"

#include <google/protobuf/util/message_differencer.h>
#include <sched.h>
#include <pthread.h>
#include <memory>

namespace bftsmartstore {

Client2Client::Client2Client(transport::Configuration *clients_config, Transport *transport,
      uint64_t client_id, uint64_t nshards, uint64_t ngroups, Partitioner *part, int group, bool signMessages, bool validateProofs,
      SintrParameters sintr_params, KeyManager *keyManager,
      EndorsementClient *endorseClient, ClientSelector *valClientSelector, std::mt19937 &rand,
      const std::vector<std::string> &keys) :
      Client2ClientCommon(client_id, clients_config, transport, group, sintr_params, endorseClient, valClientSelector, rand, keys),
      nshards(nshards), ngroups(ngroups), signMessages(signMessages), validateProofs(validateProofs), keyManager(keyManager) {

  valClient = new ValidationClient(transport, client_id, sintr_params, part, nshards, ngroups);
  Warning("CLIENT2CLIENT HOTSTUFF CREATED FOR CLIENT ID %d", client_id);
}

Client2Client::~Client2Client() {
  delete valClient;
}

void Client2Client::ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data, void *meta_data) {
  if (type == sendPing.GetTypeName()) {
    PingMessage ping;
    Debug("Ping received");
    ping.ParseFromString(data);
    HandlePingMessage(ping);
  }
  else if (type == beginValTxnMsg.GetTypeName()) {
    ManageDispatchBeginValidateTxnMessage(remote, data);
  }
  else if (type == fwdReadResultMsg.GetTypeName()) {
    ManageDispatchForwardReadResultMessage(remote, data);
  }
  else if (type == finishValTxnMsg.GetTypeName()) {
    ManageDispatchFinishValidateTxnMessage(remote, data);
  }
  else {
    Panic("Received unexpected message type: %s", type.c_str());
  }
}

void Client2Client::SendBeginValidateTxnMessage(uint64_t client_seq_num, const TxnState &protoTxnState, uint64_t txnStartTime, PolicyClient *policyClient) {

  if (sintr_params.clientEstimatePolicy) {
    UW_ASSERT(policyClient != nullptr);
    endorseClient->SetClientSeqNum(client_seq_num); // need to set this here for updates
  }
  else {
    // no estimate, so no need to send any begin validate messages
    UW_ASSERT(policyClient == nullptr);
    // still some bookkeeping to do
    ResetTrackingState();
    std::unique_lock lock(seq_num_lock);
    this->client_seq_num = client_seq_num;
    endorseClient->SetClientSeqNum(client_seq_num);
    lock.unlock();
    beginValSent.insert(client_id);
    return;
  }
  
  if (!sintr_params.c2cSendThread) {
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

void Client2Client::SendBeginValidateTxnMessageHelper(const uint64_t client_seq_num, const TxnState &protoTxnState, uint64_t txnStartTime,
    PolicyClient *policyClient) {
  UW_ASSERT(policyClient != nullptr);
  if(sintr_params.clientEstimatePolicy) {
    ResetTrackingState();
    std::unique_lock lock(seq_num_lock);
    Debug("Setting client sequence number here to %lu from %lu", client_seq_num, this->client_seq_num);
    this->client_seq_num = client_seq_num;
    lock.unlock();
  }
  // for tracking purposes, must have self in beginValSent
  beginValSent.insert(client_id);

  sentBeginValTxnMsg.Clear();
  proto::BeginValidateTxn beginValTxn;
  beginValTxn.mutable_timestamp()->set_timestamp(txnStartTime);
  beginValTxn.mutable_timestamp()->set_id(client_id);
  beginValTxn.set_client_id(client_id);
  std::shared_lock lock(seq_num_lock);
  beginValTxn.set_client_seq_num(client_seq_num);
  lock.unlock();
  *beginValTxn.mutable_txn_state() = protoTxnState;

  if (sintr_params.signFwdReadResults) {
    CreateHMACedMessage(
      beginValTxn,
      *sentBeginValTxnMsg.mutable_signed_begin_validate_txn()
    );
  }
  else {
    *sentBeginValTxnMsg.mutable_begin_validate_txn() = std::move(beginValTxn);
  }

  Debug("beginValTxnMsg client id %lu, seq num %lu", client_id, client_seq_num);

  // get clients to contact based on heuristic
  std::set<uint64_t> clients = ProcessClientValidationHeuristic(policyClient);

  for (const auto &i : clients) {
    // do not send to self
    if (i == client_id) {
      continue;
    }
    beginValSent.insert(i);
    transport->SendMessageToReplica(this, i, sentBeginValTxnMsg);
  }
}

void Client2Client::SendForwardReadResultMessage(const std::string &key, const std::string &value,
    const proto::CommitProof &proof, const Timestamp &timestamp, const proto::SignedMessage &signedMsg) {

  if (!sintr_params.c2cSendThread) {
    SendForwardReadResultMessageHelper(
      key, value, proof, timestamp, signedMsg
    );
  }
  else {
    std::function<void*(void)> f = [=]() {
      this->SendForwardReadResultMessageHelper(
        key, value, proof, timestamp, signedMsg
      );
      return (void*) true;
    };
    
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

void Client2Client::SendForwardReadResultMessageHelper(const std::string &key, const std::string &value,
    const proto::CommitProof &proof, const Timestamp &timestamp, const proto::SignedMessage &signedMsg) {
  SentFwdResultState *sentFwdResultState = new SentFwdResultState();
  proto::ForwardReadResultMessage *fwdReadResultMsgToSend = new proto::ForwardReadResultMessage();
  proto::ForwardReadResult *fwdReadResult = new proto::ForwardReadResult();
  fwdReadResult->set_key(key);
  fwdReadResult->set_value(value);
  fwdReadResult->set_client_id(client_id);
  std::shared_lock seq_lock(seq_num_lock);
  // not sure if necessary to acquire this lock
  fwdReadResult->set_client_seq_num(client_seq_num);
  seq_lock.unlock();

  fwdReadResult->mutable_timestamp()->set_timestamp(timestamp.getTimestamp());
  fwdReadResult->mutable_timestamp()->set_id(timestamp.getID());

  // copy into sentFwdResultState
  sentFwdResultState->fwdMsgUnderlying = fwdReadResult;
  
  if (sintr_params.signFwdReadResults) {
    CreateHMACedMessage(
      *fwdReadResult,
      *fwdReadResultMsgToSend->mutable_signed_fwd_read_result(),
      beginValSent
    );
  }
  else {
    fwdReadResultMsgToSend->set_allocated_fwd_read_result(fwdReadResult);
  }

  if (validateProofs && (timestamp.getID() != 0 || timestamp.getTimestamp() != 0)) {
    *fwdReadResultMsgToSend->mutable_commit_proof() = proof;
    Debug("Proof writeback msg status is %lu", proof.writeback_message().status());
  }
  *fwdReadResultMsgToSend->mutable_server_read_sig() = signedMsg;

  std::unique_lock lock(sentFwdResultsMutex);
  sentFwdResultState->fwdMsgSigned = fwdReadResultMsgToSend;
  sentFwdResults.insert(sentFwdResultState);

  Debug(
    "ForwardReadResult: client id %lu, seq num %lu, key %s",
    client_id,
    client_seq_num,
    BytesToHex(key, 16).c_str()
  );
  for (const auto &i : beginValSent) {
    // do not send to self
    if (i == client_id) {
      continue;
    }
    transport->SendMessageToReplica(this, i, *fwdReadResultMsgToSend);
  }
}

void Client2Client::ManageDispatchBeginValidateTxnMessage(const TransportAddress &remote, const std::string &data) {
  if (!sintr_params.c2cReceiveThread) {
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
  if (!sintr_params.c2cReceiveThread) {
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

void Client2Client::ManageDispatchFinishValidateTxnMessage(const TransportAddress &remote, const std::string &data) {
  if (!sintr_params.c2cReceiveThread && !sintr_params.parallelEndorsementCheck) {
    finishValTxnMsg.ParseFromString(data);
    std::shared_ptr<proto::SignedMessage> signedMsg(finishValTxnMsg.release_signed_validation_txn_digest());

    if (sintr_params.optimisticReceiveEndorsement) {
      HandleFinishValidateTxnMessageOptimistic(finishValTxnMsg, signedMsg);
    }

    HandleFinishValidateTxnMessage(finishValTxnMsg, signedMsg);
  }
  else {
    proto::FinishValidateTxnMessage *finishValTxnMsg = new proto::FinishValidateTxnMessage();
    finishValTxnMsg->ParseFromString(data);
    std::shared_ptr<proto::SignedMessage> signedMsg(finishValTxnMsg->release_signed_validation_txn_digest());

    auto f = [this, finishValTxnMsg, signedMsg](){
      this->HandleFinishValidateTxnMessage(*finishValTxnMsg, signedMsg);
      delete finishValTxnMsg;
      return (void*) true;
    };

    if (sintr_params.optimisticReceiveEndorsement) {
      HandleFinishValidateTxnMessageOptimistic(*finishValTxnMsg, signedMsg);
    }

    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    if (sintr_params.parallelEndorsementCheck) {
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

  proto::BeginValidateTxn beginValTxn;
  if (sintr_params.signFwdReadResults) {
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
  const TxnState &txnState = beginValTxn.txn_state();
  Debug(
    "HandleBeginValidateTxnMessage: from client id %lu, seq num %lu", 
    curr_client_id, 
    curr_client_seq_num
  );
  ValidationTransaction *valTxn = valParseClient->Parse(txnState);
  TransportAddress *remoteCopy = remote.clone();
  ValidationInfo *valInfo = new ValidationInfo(curr_client_id, curr_client_seq_num, std::move(valTxn), std::move(remoteCopy));
  valClient->SetTxnTimestamp(curr_client_id, curr_client_seq_num, Timestamp(beginValTxn.timestamp()));
  validationQueue.push(valInfo);
}

void Client2Client::HandleForwardReadResultMessage(const proto::ForwardReadResultMessage &fwdReadResultMsg) {

  proto::ForwardReadResult fwdReadResult;
  if (sintr_params.signFwdReadResults) {
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

  if (sintr_params.clientCheckEvidence) {
    if (!sintr_params.parallelQuerySigsCheck) {
      if (!CheckPreparedCommittedEvidence(fwdReadResult, fwdReadResultMsg)) {
        Panic("Invalid prepared or committed evidence on forwarded query result");
        return;
      }
    }
    else {
      Debug("HandleForwardReadReuslt parallel query sig check: from client id %lu, seq num %lu, key %s, value %s",
        curr_client_id, 
        curr_client_seq_num,
        BytesToHex(curr_key, 16).c_str(),
        BytesToHex(curr_key, 16).c_str()
      );
      // this will be async so no need to check the result
      CheckPreparedCommittedEvidence(fwdReadResult, fwdReadResultMsg);
      // but still tell valClient to maintain order of readset
      // failed check will later stop validation
    }
  }

  Debug(
    "HandleForwardReadResult: from client id %lu, seq num %lu, key %s, value %s", 
    curr_client_id, 
    curr_client_seq_num,
    BytesToHex(curr_key, 16).c_str(),
    BytesToHex(curr_value, 16).c_str()
  );
  // tell valClient about this forwardedReadResult
  valClient->ProcessForwardReadResult(curr_client_id, curr_client_seq_num, std::move(fwdReadResult));
}

void Client2Client::HandleFinishValidateTxnMessage(const proto::FinishValidateTxnMessage &finishValTxnMsg,
    std::shared_ptr<proto::SignedMessage> signedMsg) {
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
  std::shared_lock lock(seq_num_lock);
  if (val_txn_seq_num != client_seq_num) {
    Debug(
      "Received stale finishValidateTxnMessage from client id %lu, seq num %lu; curr seq num %lu", 
      peer_client_id, 
      val_txn_seq_num,
      client_seq_num
    );
    return;
  }
  if(sintr_params.optimisticReceiveEndorsement) {
    lock.unlock();
  }

  std::string valTxnDigest;
  if (sintr_params.signFinishValidation) {
    // verify signature
    if (signedMsg == nullptr) {
      Debug("Missing signed validation txn digest sent from client id %lu", peer_client_id);
      return;
    }

    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    if(!CheckSignature(*signedMsg, keyManager, true)) {
      Debug("Invalid signature on validation txn digest sent from client id %lu", peer_client_id);
      return;
    }
    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // verify_endorse_us.add(duration);

    valTxnDigest = signedMsg->packed_msg();
  }
  else {
    // dummy signed message
    UW_ASSERT(signedMsg == nullptr);
    signedMsg = std::make_shared<proto::SignedMessage>();
    signedMsg->set_replica_id(peer_client_id);
    signedMsg->set_packed_msg(finishValTxnMsg.validation_txn_digest());
    signedMsg->set_signature("");
    valTxnDigest = finishValTxnMsg.validation_txn_digest();
  }

  Debug("HandleFinishValidateTxnMessage: txn digest %s for client %lu from client %lu with seq number %lu",
    BytesToHex(valTxnDigest, 16).c_str(), client_id, peer_client_id,val_txn_seq_num);

  if (sintr_params.debugEndorseCheck) {
    std::unique_ptr<proto::Transaction> debug_txn = std::make_unique<proto::Transaction>(finishValTxnMsg.val_txn_msg());
    endorseClient->DebugCheck(std::move(debug_txn));
  }

  if (!sintr_params.optimisticReceiveEndorsement) {
    endorseClient->AddValidation(peer_client_id, valTxnDigest, signedMsg, client_seq_num);
  }
  // in optimistic case, endorsement is added outside so just check
  else {
    endorseClient->CheckValidation(peer_client_id, val_txn_seq_num, valTxnDigest);
  }
}

void Client2Client::HandleFinishValidateTxnMessageOptimistic(const proto::FinishValidateTxnMessage &finishValTxnMsg,
    std::shared_ptr<proto::SignedMessage> signedMsg) {
  uint64_t peer_client_id = finishValTxnMsg.client_id();
  uint64_t val_txn_seq_num = finishValTxnMsg.validation_txn_seq_num();
  // stale finish validation message
  std::shared_lock lock(seq_num_lock);
  if (val_txn_seq_num != client_seq_num) {
    Debug(
      "Received stale finishValidateTxnMessage from client id %lu, seq num %lu; curr seq num %lu", 
      peer_client_id, 
      val_txn_seq_num,
      client_seq_num
    );
    return;
  }

  if (sintr_params.signFinishValidation) {
    UW_ASSERT(signedMsg != nullptr);
    endorseClient->AddValidationOptimistic(peer_client_id, signedMsg, client_seq_num);
  }
  else {
    // dummy signed message
    UW_ASSERT(signedMsg == nullptr);
    signedMsg = std::make_shared<proto::SignedMessage>();
    signedMsg->set_replica_id(peer_client_id);
    signedMsg->set_packed_msg(finishValTxnMsg.validation_txn_digest());
    signedMsg->set_signature("");
    endorseClient->AddValidationOptimistic(
      peer_client_id,
      signedMsg,
      client_seq_num
    );
  }
}

bool Client2Client::CheckPreparedCommittedEvidence(const proto::ForwardReadResult &fwdReadResult,
    const proto::ForwardReadResultMessage &fwdReadResultMsg) {

  if(sintr_params.parallelQuerySigsCheck) {
    // TODO: We copy the shared pointer instead of the message -> should be less overhead
    auto f = [this, fwdReadResult, fwdReadResultMsg] {
      Debug("Checking signatures asynchronously for %lu : %lu", fwdReadResult.client_id(), fwdReadResult.client_seq_num());

      if(!this->CheckReadSigHelper(fwdReadResultMsg.server_read_sig(), fwdReadResultMsg.commit_proof(), fwdReadResult.key(), fwdReadResult.value(), fwdReadResult.timestamp())) {
        Panic("Invalid signatures for read result!");
      } else {
        valClient->NotifyForwardReadResultValid(fwdReadResult.client_id(), fwdReadResult.client_seq_num());
      }
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    parallelSigCheckQueue.push(executor);
    return true;
  } else {
    return CheckReadSigHelper(fwdReadResultMsg.server_read_sig(), fwdReadResultMsg.commit_proof(), fwdReadResult.key(), fwdReadResult.value(), fwdReadResult.timestamp());
  }
}

bool Client2Client::CheckReadSigHelper(const proto::SignedMessage &signedMessage, const proto::CommitProof &proof,
    const std::string &key, const std::string &value, const Timestamp &ts) {
  std::string type;
  std::string data;
  if(!ValidateSignedMessage(signedMessage, keyManager, data, type)) {
    Debug("signature was invalid");
    return false;
  }
  proto::ReadReply readReply;
  if(type == readReply.GetTypeName()) {
    readReply.ParseFromString(data);
    if (readReply.key() != key) {
      Panic("Key mismatch: expected %s, got %s", key.c_str(), readReply.key().c_str());
    }
    if (readReply.value() != value) {
      Panic("Value mismatch: expected %s, got %s", BytesToHex(value, 16).c_str(), BytesToHex(readReply.value(), 16).c_str());
    }
    if (Timestamp(readReply.value_timestamp()) != ts) {
      Panic("Timestamp mismatch");
    }
    if((ts.getID() != 0 || ts.getTimestamp() != 0) && validateProofs) {
      UW_ASSERT(google::protobuf::util::MessageDifferencer::Equals(readReply.commit_proof(), proof));
    }
  } else {
    Warning("Signed message not of type readreply");
    return false;
  }
  return ValidateReadProof(proof, key, value, ts);
}

const ::google::protobuf::Message *Client2Client::GetSentBeginValTxnMsg() const {
  return &sentBeginValTxnMsg;
};

void Client2Client::ValidationThreadFunction() {

  auto preValFunc = [](){};
  auto postValFunc = [this](transaction_status_t result, ValidationInfoBase *valInfo) {
    if (result != COMMITTED) {
      return;
    }

    uint64_t curr_client_id = valInfo->txn_client_id;
    uint64_t curr_client_seq_num = valInfo->txn_client_seq_num;

    std::unique_ptr<proto::Transaction> txn = valClient->GetCompletedTxn(curr_client_id, curr_client_seq_num);

    proto::FinishValidateTxnMessage finishValTxnMsg;
    finishValTxnMsg.set_client_id(client_id);
    finishValTxnMsg.set_validation_txn_seq_num(curr_client_seq_num);

    std::string digest = TransactionDigest(*txn);
    Debug("Validation Digest is : %s", BytesToHex(digest, 16).c_str());
    if (sintr_params.signFinishValidation) {
      // sign the digest
      SignBytes(
        digest, 
        keyManager->GetPrivateKey(keyManager->GetClientKeyId(client_id)), 
        client_id, 
        *finishValTxnMsg.mutable_signed_validation_txn_digest()
      );
    }
    else {
      finishValTxnMsg.set_validation_txn_digest(digest);
    }

    if (sintr_params.debugEndorseCheck) {
      finishValTxnMsg.set_allocated_val_txn_msg(txn.release());
    }
    if (false) {
      Debug("Trying to send validation txn: [%lu:%lu] %s", curr_client_id, curr_client_seq_num, BytesToHex(digest, 16).c_str());
      for (const auto &read : txn->readset()) {
        Debug("Validation read key: %s", BytesToHex(read.key(), 16).c_str());
      }
      for (const auto &write : txn->writeset()) {
        Debug("Validation write key: %s", BytesToHex(write.key(), 16).c_str());
        Debug("Validation write value: %s", BytesToHex(write.value(), 16).c_str());

      }
    }

    transport->SendMessage(this, *valInfo->remote, finishValTxnMsg);
  };

  ValidationThreadFunctionBase(valClient, preValFunc, postValFunc);
}

bool Client2Client::ValidateHMACedMessage(const proto::SignedMessage &signedMessage, std::string &data) {
  data = signedMessage.packed_msg();
  proto::HMACs hmacs;
  hmacs.ParseFromString(signedMessage.signature());
  return crypto::verifyHMAC(
    signedMessage.packed_msg(),
    (*hmacs.mutable_hmacs())[client_id],
    sessionKeys[signedMessage.replica_id() % clients_config->n]
  );
}

void Client2Client::CreateHMACedMessage(const ::google::protobuf::Message &msg,
    ::google::protobuf::Message &signedMessage, const std::string &signedTypeName) {
  if (signedTypeName == fwdReadResultMsg.GetTypeName()) {
    CreateHMACedMessage(
      msg,
      *dynamic_cast<proto::ForwardReadResultMessage&>(signedMessage).mutable_signed_fwd_read_result()
    );
  }
  else {
    Panic("Unknown signed message type %s", signedTypeName.c_str());
  }
  CreateHMACedMessage(msg, dynamic_cast<proto::SignedMessage&>(signedMessage), signedTypeName);
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
  signedMessage.set_packed_msg(msgData);
  signedMessage.set_replica_id(client_id);
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

bool Client2Client::ValidateReadProof(const proto::CommitProof& commitProof, const std::string& key,
    const std::string& value, const Timestamp& timestamp) {
  // hack for load:
  if (timestamp.getID() == 0 && timestamp.getTimestamp() == 0) {
    Debug("Using preloaded key");
    return true;
  }

  // First, verify the transaction
  Debug("Validating read proof");

  // txn must have timestamp of write
  if (Timestamp(commitProof.txn().timestamp()) != timestamp) {
    return false;
  }
  Debug("timestamp valid");

  bool found_write = false;

  for (const auto& write : commitProof.txn().writeset()) {
    if (write.key() == key && write.value() == value) {
      found_write = true;
      break;
    }
  }

  if (!found_write) {
    return false;
  }
  Debug("write valid");

  // Verified Transaction at this point

  // Next, verify that the decision is valid for the transaction

  std::string proofTxnDigest = TransactionDigest(commitProof.txn());

  // make sure the writeback message is for the transaction
  if (commitProof.writeback_message().txn_digest() != proofTxnDigest) {
    return false;
  }
  Debug("commit digest valid");

  if (commitProof.writeback_message().status() != REPLY_OK) {
    return false;
  }
  Debug("writeback status valid");

  if (!verifyGDecision(commitProof.writeback_message(), commitProof.txn(), keyManager, signMessages, 1)) {
    // assume f = 1, TODO: Change to use config
    return false;
  }
  Debug("proof valid");

  return true;
}

} // namespace pelotonstore