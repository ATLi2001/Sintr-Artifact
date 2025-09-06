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

#include "store/pelotonstore/client2client.h"
#include "store/pelotonstore/validation_client.h"
#include "store/pelotonstore/pbft_batched_sigs.h"
#include "store/common/frontend/validation_transaction.h"
#include "store/common/util.h"

#include <google/protobuf/util/message_differencer.h>
#include <sched.h>
#include <pthread.h>
#include <memory>

namespace pelotonstore {

Client2Client::Client2Client(transport::Configuration *clients_config, Transport *transport,
      uint64_t client_id, uint64_t nshards, uint64_t ngroups, int group, bool signMessages, bool validateProofs,
      SintrParameters sintr_params, KeyManager *keyManager,
      EndorsementClient *endorseClient, ClientSelector *valClientSelector, std::mt19937 &rand,
      const std::vector<std::string> &keys) :
      Client2ClientCommon(client_id, clients_config, transport, group, sintr_params, endorseClient, valClientSelector, rand, keys),
      nshards(nshards), ngroups(ngroups), signMessages(signMessages), validateProofs(validateProofs), keyManager(keyManager) {

  valClient = new ValidationClient(transport, client_id, sintr_params);
  Warning("CLIENT2CLIENT PELOTON CREATED FOR CLIENT ID %d", client_id);
}

Client2Client::~Client2Client() {
  delete valClient;
}

void Client2Client::ReceiveMessage(const TransportAddress &remote,
      const std::string &type, const std::string &data, void *meta_data) {

  if (type == beginValTxnMsg.GetTypeName()) {
    ManageDispatchBeginValidateTxnMessage(remote, data);
  }
  else if (type == fwdSQLResultMsg.GetTypeName()) {
    ManageDispatchForwardSQLResultMessage(remote, data);
  }
  else if (type == finishValTxnMsg.GetTypeName()) {
    ManageDispatchFinishValidateTxnMessage(remote, data);
  }
  else {
    Panic("Received unexpected message type: %s", type.c_str());
  }
}

void Client2Client::SendBeginValidateTxnMessage(uint64_t client_seq_num, const TxnState &protoTxnState, PolicyClient *policyClient) {

  if (sintr_params.clientEstimatePolicy) {
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
  
  if (!sintr_params.c2cSendThread) {
    SendBeginValidateTxnMessageHelper(client_seq_num, protoTxnState, policyClient);
    delete policyClient;
  }
  else {
    auto f = [=]() {
      this->SendBeginValidateTxnMessageHelper(
        client_seq_num, protoTxnState, policyClient
      );
      delete policyClient;
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

void Client2Client::SendBeginValidateTxnMessageHelper(const uint64_t client_seq_num, const TxnState &protoTxnState,
    PolicyClient *policyClient) {
  UW_ASSERT(policyClient != nullptr);

  ResetTrackingState();
  this->client_seq_num = client_seq_num;
  // for tracking purposes, must have self in beginValSent
  beginValSent.insert(client_id);

  sentBeginValTxnMsg.Clear();
  proto::BeginValidateTxn beginValTxn;
  beginValTxn.set_client_id(client_id);
  beginValTxn.set_client_seq_num(client_seq_num);
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

void Client2Client::SendForwardSQLResultMessage(const std::string &sql_gen_id, const std::string &sql_result,
    proto::SignedMessage *signedMessage, TransactionMessage *txn_msg) {

  if (!sintr_params.c2cSendThread) {
    SendForwardSQLResultMessageHelper(
      sql_gen_id, sql_result,
      signedMessage, txn_msg
    );
  }
  else {
    std::function<void*(void)> f = [=]() {
      this->SendForwardSQLResultMessageHelper(
        sql_gen_id, sql_result,
        signedMessage, txn_msg
      );
      return (void*) true;
    };
    
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cSendQueue.push(executor);
  }
}

void Client2Client::SendForwardSQLResultMessageHelper(const std::string &sql_gen_id, const std::string &sql_result,
      proto::SignedMessage *signedMessage, TransactionMessage *txn_msg) {

  SentFwdResultState *sentFwdResultState = new SentFwdResultState();
  proto::ForwardSQLResultMessage *fwdSQLResultMsgToSend = new proto::ForwardSQLResultMessage();
  proto::ForwardSQLResult *fwdSQLResult = new proto::ForwardSQLResult();
  fwdSQLResult->set_sql_gen_id(sql_gen_id);
  fwdSQLResult->set_sql_result(sql_result);
  fwdSQLResult->set_client_id(client_id);
  fwdSQLResult->set_client_seq_num(client_seq_num);
  fwdSQLResult->set_allocated_txn_msg(txn_msg); //TODO: Figure out a better way to move this than copying

  // copy into sentFwdResultState
  sentFwdResultState->fwdMsgUnderlying = fwdSQLResult;
  
  if (sintr_params.signFwdReadResults) {
    CreateHMACedMessage(
      *fwdSQLResult,
      *fwdSQLResultMsgToSend->mutable_signed_fwd_sql_result(),
      beginValSent
    );
  }
  else {
    fwdSQLResultMsgToSend->set_allocated_fwd_sql_result(fwdSQLResult);
  }

  if (validateProofs) {
    fwdSQLResultMsgToSend->set_allocated_server_sql_sig(signedMessage);
  }

  std::unique_lock lock(sentFwdResultsMutex);
  sentFwdResultState->fwdMsgSigned = fwdSQLResultMsgToSend;
  sentFwdResults.insert(sentFwdResultState);

  Debug(
    "ForwardSQLResult: client id %lu, seq num %lu, sql gen id %s",
    client_id,
    client_seq_num,
    BytesToHex(sql_gen_id, 16).c_str()
  );
  for (const auto &i : beginValSent) {
    // do not send to self
    if (i == client_id) {
      continue;
    }
    transport->SendMessageToReplica(this, i, *fwdSQLResultMsgToSend);
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

void Client2Client::ManageDispatchForwardSQLResultMessage(const TransportAddress &remote, const std::string &data) {
  if (!sintr_params.c2cReceiveThread) {
    fwdSQLResultMsg.ParseFromString(data);
    HandleForwardSQLResultMessage(fwdSQLResultMsg);
  }
  else {
    proto::ForwardSQLResultMessage *fwdSQLResultMsg = new proto::ForwardSQLResultMessage();
    fwdSQLResultMsg->ParseFromString(data);
    auto f = [this, fwdSQLResultMsg](){
      this->HandleForwardSQLResultMessage(*fwdSQLResultMsg);
      delete fwdSQLResultMsg;
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
  valClient->SetTxnTimestamp(curr_client_id, curr_client_seq_num);
  validationQueue.push(valInfo);
}

void Client2Client::HandleForwardSQLResultMessage(const proto::ForwardSQLResultMessage &fwdSQLResultMsg) {

  proto::ForwardSQLResult fwdSQLResult;
  if (sintr_params.signFwdReadResults) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;

    // first check client signature
    // Debugs will not include client ID/client seq num because they are included in the fwdSQLResult
    if (!fwdSQLResultMsg.has_signed_fwd_sql_result()) {
      Debug("Missing client signature on forwarded sql result");
      return;
    }
    std::string data;
    if (!ValidateHMACedMessage(fwdSQLResultMsg.signed_fwd_sql_result(), data)) {
      Debug("Invalid client signature on forwarded sql result");
      return;
    }

    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // verify_hmac_us.add(duration);

    fwdSQLResult.ParseFromString(data);
  }
  else {
    fwdSQLResult = fwdSQLResultMsg.fwd_sql_result();
  }

  uint64_t curr_client_id = fwdSQLResult.client_id();
  uint64_t curr_client_seq_num = fwdSQLResult.client_seq_num();

  std::string curr_sql_gen_id = fwdSQLResult.sql_gen_id();
  std::string curr_sql_result = fwdSQLResult.sql_result();

  if (sintr_params.clientCheckEvidence) {
    if (!sintr_params.parallelQuerySigsCheck) {
      if (!CheckPreparedCommittedEvidence(fwdSQLResult, fwdSQLResultMsg)) {
        Panic("Invalid prepared or committed evidence on forwarded query result");
        return;
      }
    }
    else {
      Debug("HandleForwardSQLResult parallel query sig check: from client id %lu, seq num %lu, sql gen id %s, sql result %s",
        curr_client_id, 
        curr_client_seq_num,
        BytesToHex(curr_sql_gen_id, 16).c_str(),
        BytesToHex(curr_sql_result, 16).c_str()
      );
      // this will be async so no need to check the result
      CheckPreparedCommittedEvidence(fwdSQLResult, fwdSQLResultMsg);
      // but still tell valClient to maintain order of readset
      // failed check will later stop validation
    }
  }

  Debug(
    "HandleForwardSQLResult: from client id %lu, seq num %lu, sql gen id %s, sql result %s", 
    curr_client_id, 
    curr_client_seq_num,
    BytesToHex(curr_sql_gen_id, 16).c_str(),
    BytesToHex(curr_sql_result, 16).c_str()
  );
  // tell valClient about this forwardedReadResult
  valClient->ProcessForwardSQLResult(curr_client_id, curr_client_seq_num, std::move(fwdSQLResult));
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
  if (val_txn_seq_num != client_seq_num) {
    Debug(
      "Received stale finishValidateTxnMessage from client id %lu, seq num %lu; curr seq num %lu", 
      peer_client_id, 
      val_txn_seq_num,
      client_seq_num
    );
    return;
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
    std::unique_ptr<TransactionMessage> debug_txn = std::make_unique<TransactionMessage>(finishValTxnMsg.val_txn_msg());
    endorseClient->DebugCheck(std::move(debug_txn));
  }

  if (!sintr_params.optimisticReceiveEndorsement) {
    endorseClient->AddValidation(peer_client_id, valTxnDigest, signedMsg);
  }
  // in optimistic case, endorsement is added outside so just check
  else {
    endorseClient->CheckValidation(peer_client_id, val_txn_seq_num, valTxnDigest);
  }
  if(!sintr_params.optimisticReceiveEndorsement && endorse_cb != nullptr && endorseClient->IsSatisfied()) {
    // only call endorse cb if optimistic endorsement set to false and other conditions are met
    Debug("CALLING ENDORSE CB FOR CLIENT %d SEQ NUM %d", peer_client_id, val_txn_seq_num);
    endorse_cb();
    endorse_cb = nullptr;
  } else {
    Debug("endorse cb is null or endorsements not satisifed for client %d seq num %d", peer_client_id, val_txn_seq_num);
  }
}

void Client2Client::HandleFinishValidateTxnMessageOptimistic(const proto::FinishValidateTxnMessage &finishValTxnMsg,
    std::shared_ptr<proto::SignedMessage> signedMsg) {
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

  if (sintr_params.signFinishValidation) {
    UW_ASSERT(signedMsg != nullptr);
    endorseClient->AddValidationOptimistic(peer_client_id, signedMsg);
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
      signedMsg
    );
  }
  if(endorse_cb != nullptr && endorseClient->IsSatisfied()) {
    // should only be called if optimistic endorsement set to true
    Debug("CALLING ENDORSE CB HERE FOR CLIENT %d SEQ NUM %d", peer_client_id, val_txn_seq_num);
    endorse_cb();
    endorse_cb = nullptr;
  } else {
    Debug("ENDORSE CB IS NULL HERE FOR CLIENT %d SEQ NUM %d", peer_client_id, val_txn_seq_num);
  }
}

bool Client2Client::CheckPreparedCommittedEvidence(const proto::ForwardSQLResult &fwdSQLResult,
    const proto::ForwardSQLResultMessage &fwdSQLResultMsg) {

  uint64_t curr_client_id = fwdSQLResult.client_id();
  uint64_t curr_client_seq_num = fwdSQLResult.client_seq_num();
  const std::string &sql_gen_id = fwdSQLResult.sql_gen_id();
  const std::string &sql_result = fwdSQLResult.sql_result();
  const TransactionMessage &sql_txn_msg = fwdSQLResult.txn_msg();

  if (validateProofs && signMessages) {
    if (!sintr_params.parallelQuerySigsCheck) {
      if (!CheckQuerySigHelper(fwdSQLResultMsg.server_sql_sig(), sql_gen_id, sql_result, sql_txn_msg)) {
        Debug(
          "Invalid server signature on forwarded sql result from client id %lu, seq num %lu",
          curr_client_id,
          curr_client_seq_num
        );
        return false;
      }
    }
    else {
      // send to worker thread
      auto f = [
        this, curr_client_id, curr_client_seq_num, signedMessage=fwdSQLResultMsg.server_sql_sig(),
        sql_gen_id, sql_result, sql_txn_msg
      ]() {
        bool is_valid = CheckQuerySigHelper(signedMessage, sql_gen_id, sql_result, sql_txn_msg);
        if (!is_valid) {
          Debug(
            "Invalid server signature on forwarded sql result from client id %lu, seq num %lu",
            curr_client_id,
            curr_client_seq_num
          );
          return (void*) false;
        }

        valClient->NotifyForwardQueryResultValid(curr_client_id, curr_client_seq_num);
        return (void*) true;
      };

      Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
      parallelSigCheckQueue.push(executor);
    }
  }

  return true;
}

bool Client2Client::CheckQuerySigHelper(const proto::SignedMessage &signedMessage,
    const std::string &sql_gen_id, const std::string &sql_result, const TransactionMessage &txnMsg) {

  crypto::PubKey* replicaPublicKey = keyManager->GetPublicKey(signedMessage.replica_id());
  if (!pelotonstore::verifyBatchedSignature(&signedMessage.signature(), &signedMessage.packed_msg(), replicaPublicKey)) {
    Debug("Invalid signature on forwarded sql result");
    return false;
  }

  proto::PackedMessage packed_msg;
  if (!packed_msg.ParseFromString(signedMessage.packed_msg())) {
    Debug("Failed to parse packed message on forwarded sql result");
    return false;
  }
  proto::SQL_RPCReply validated_result;
  if (!validated_result.ParseFromString(packed_msg.msg())) {
    Debug("Failed to parse validated result on forwarded sql result");
    return false;
  }

  // next make sure that we have matches
  if (validated_result.sql_gen_id() != sql_gen_id) {
    Debug("Mismatch in sql gen id for forwarded sql result %s vs %s", BytesToHex(sql_gen_id, 16).c_str(), BytesToHex(validated_result.sql_gen_id(), 16).c_str());
    return false;
  }

  if (validated_result.sql_res() != sql_result) {
    Debug("Mismatch in sql result for forwarded sql result");
    return false;
  }

  if (TransactionDigest(txnMsg) != TransactionDigest(validated_result.txn_msg())) {
    Debug("Mismatch in read set or write set for forwarded query result");
    return false;
  }

  return true;
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

    std::unique_ptr<TransactionMessage> txn_msg = valClient->GetCompletedTxnMsg(curr_client_id, curr_client_seq_num);

    proto::FinishValidateTxnMessage finishValTxnMsg;
    finishValTxnMsg.set_client_id(client_id);
    finishValTxnMsg.set_validation_txn_seq_num(curr_client_seq_num);

    std::string digest = TransactionDigest(*txn_msg);
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
      finishValTxnMsg.set_allocated_val_txn_msg(txn_msg.release());
    }
    if (false) {
      Debug("Trying to send validation txn: [%lu:%lu]", curr_client_id, curr_client_seq_num);
      for (const auto &read : txn_msg->readset()) {
        Debug("Validation read key: %s", read.key().c_str());
      }
      for (const auto &write : txn_msg->writeset()) {
        Debug("Validation write key: %s", write.key().c_str());
        Debug("Validation write value: %s", write.value().c_str());

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
  if (signedTypeName == fwdSQLResultMsg.GetTypeName()) {
    CreateHMACedMessage(
      msg,
      *dynamic_cast<proto::ForwardSQLResultMessage&>(signedMessage).mutable_signed_fwd_sql_result()
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

void Client2Client::SetEndorsementCallback(endorsement_callback endorse_cb) {
  if (!sintr_params.c2cReceiveThread) {
    SetEndorsementCallbackHelper(endorse_cb);
  }
  else {
    std::function<void*(void)> f = [=]() {
      this->SetEndorsementCallbackHelper(endorse_cb);
      return (void*) true;
    };
    Client2ClientExecutor *executor = new Client2ClientExecutor(std::move(f));
    c2cReceiveQueue.push(executor);
  }
}

void Client2Client::SetEndorsementCallbackHelper(endorsement_callback endorse_cb) {
  // check if endorsements have been satisfied now 
  // case where endorsements arrive after client tries to commit but before endorse_cb is set
  if(endorseClient->IsSatisfied()) {
    Warning("RUNNING ENDORSE CB HERE");
    endorse_cb();
  } else {
    Debug("SET ENDORSE CB HERE");
    this->endorse_cb = endorse_cb;
  }
}

} // namespace pelotonstore
