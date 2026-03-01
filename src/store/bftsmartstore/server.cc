/***********************************************************************
 *
 * Copyright 2021 Florian Suri-Payer <fsp@cs.cornell.edu>
 *                Zheng Wang <zw494@cornell.edu>
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
#include "store/bftsmartstore/server.h"
#include "store/bftsmartstore/common.h"
#include "store/bftsmartstore/serverclient.h"
#include "store/common/transaction.h"
#include "store/common/common-proto.pb.h"
#include "store/common/frontend/sync_client.h"
#include "store/common/sintring/validation_parse_client.h"
#include <iostream>
#include <sys/time.h>

namespace bftsmartstore {

using namespace std;

Server::Server(const transport::Configuration& config, KeyManager *keyManager,
  int groupIdx, int idx, int numShards, int numGroups, bool signMessages,
  bool validateProofs, SintrParameters sintr_params, uint64_t timeDelta, Partitioner *part, Transport* tp,
  bool order_commit, bool validate_abort, bool execTxnServerSide,
  TrueTime timeServer) : config(config), keyManager(keyManager),
  groupIdx(groupIdx), idx(idx), id(groupIdx * config.n + idx),
  numShards(numShards), numGroups(numGroups), signMessages(signMessages),
  validateProofs(validateProofs),  timeDelta(timeDelta), part(part), tp(tp),
  order_commit(order_commit), validate_abort(validate_abort),
  timeServer(timeServer), sintr_params(sintr_params), execTxnServerSide(execTxnServerSide) {
  dummyProof = std::make_shared<proto::CommitProof>();

  dummyProof->mutable_writeback_message()->set_status(REPLY_OK);
  dummyProof->mutable_writeback_message()->set_txn_digest("");
  proto::ShardSignedDecisions dec;
  *dummyProof->mutable_writeback_message()->mutable_signed_decisions() = dec;

  dummyProof->mutable_txn()->mutable_timestamp()->set_timestamp(0);
  dummyProof->mutable_txn()->mutable_timestamp()->set_id(0);
  Notice("Loading Policy Store from config file: %s. ", sintr_params.policyConfigPath.c_str());
  LoadPolicyStore(sintr_params.policyConfigPath);
  policyIdFunction = GetPolicyIdFunction(sintr_params.policyFunctionName);
}

Server::~Server() {
  delete syncClientForExec;
  delete serverClientForExec;
}

bool Server::CCC2(const proto::Transaction& txn) {
  Debug("Starting ccc v2 check");
  Timestamp txTs(txn.timestamp());
  for (const auto &read : txn.readset()) {
    if(!IsKeyOwned(read.key())) {
      continue;
    }

    // we want to make sure that our reads don't span any
    // committed/prepared writes

    // check the committed writes
    Timestamp rts(read.readtime());
    // we want to make sure there are no committed writes for this key after
    // the rts and before the txTs
    std::vector<std::pair<Timestamp, Server::ValueAndProof>> committedWrites;
    if (commitStore.getCommittedAfter(read.key(), rts, committedWrites)) {
      for (const auto& committedWrite : committedWrites) {
        if (committedWrite.first < txTs) {
          Debug("found committed conflict with read for key: %s", read.key().c_str());
          return false;
        }
      }
    }

    // check the prepared writes
    const auto preparedWritesItr = preparedWrites.find(read.key());
    if (preparedWritesItr != preparedWrites.end()) {
      for (const auto& writeTs : preparedWritesItr->second) {
        if (rts < writeTs && writeTs < txTs) {
          Debug("found prepared conflict with read for key: %s", read.key().c_str());
          return false;
        }
      }
    }

  }

  Debug("checked all reads");

  for (const auto &write : txn.writeset()) {
    if(!IsKeyOwned(write.key())) {
      continue;
    }

    // we want to make sure that no prepared/committed read spans
    // our writes

    // check commited reads
    // for (const auto& read : committedReads[write.key()]) {
    //   // second is the read ts, first is the txTs that did the read
    //   if (read.second < txTs && txTs < read.first) {
    //       Debug("found committed conflict with write for key: %s", write.key().c_str());
    //       return false;
    //     }
    //   }

    auto committedReadsItr = committedReads.find(write.key());

    if (committedReadsItr != committedReads.end() && committedReadsItr->second.size() > 0) {

      for (auto read = committedReadsItr->second.rbegin(); read != committedReadsItr->second.rend(); ++read) {
      //for (const auto& read : committedReads[write.key()]) {
        // second is the read ts, first is the txTs that did the read
        if (txTs >= read->first){
          break;
        }
        if (read->second < txTs && txTs < read->first) {
            Debug("found committed conflict with write for key: %s", write.key().c_str());
            return false;
        }

      }
    }

    // check prepared reads
    for (const auto& read : preparedReads[write.key()]) {
      // second is the read ts, first is the txTs that did the read
      if (read.second < txTs && txTs < read.first) {
          Debug("found prepared conflict with write for key: %s", write.key().c_str());
          return false;
      }
    }
  }
  return true;
}

bool Server::CCC(const proto::Transaction& txn) {
  Debug("Starting ccc check");
  Timestamp txTs(txn.timestamp());
  for (const auto &read : txn.readset()) {
    if(!IsKeyOwned(read.key())) {
      continue;
    }

    // we want to make sure that our reads don't span any
    // committed/prepared writes

    // check the committed writes
    Timestamp rts(read.readtime());
    Timestamp upper;
    // this is equivalent to checking if there is a write with a timestamp t
    // such that t > rts and t < txTs
    if (commitStore.getUpperBound(read.key(), rts, upper)) {
      if (upper < txTs) {
        Debug("found committed conflict with read for key: %s", read.key().c_str());
        return false;
      }
    }

    // check the prepared writes
    for (const auto& pair : pendingTransactions) {
      for (const auto& write : pair.second.writeset()) {
        if (write.key() == read.key()) {
          Timestamp wts(pair.second.timestamp());
          if (wts > rts && wts < txTs) {
            Debug("found prepared conflict with read for key: %s", read.key().c_str());
            return false;
          }
        }
      }
    }
  }

  Debug("checked all reads");

  for (const auto &write : txn.writeset()) {
    if(!IsKeyOwned(write.key())) {
      continue;
    }

    // we want to make sure that no prepared/committed read spans
    // our writes

    // check commited reads
    // get a pointer to the first read that commits after this tx
    auto it = committedReads[write.key()].lower_bound(txTs);
    if (it != committedReads[write.key()].end()) {
      // if the iterator is at the end, then that means there are no committed reads
      // before this tx
      it++;
      // all iterator pairs committed after txTs (commit ts > txTs)
      // so we just need to check if they returned a version before txTs (read ts < txTs)
      while(it != committedReads[write.key()].end()) {
        if ((*it).second < txTs) {
          Debug("found committed conflict with write for key: %s", write.key().c_str());
          return false;
        }
        it++;
      }
    }

    // next, check the prepared tx's read sets
    for (const auto& pair : pendingTransactions) {
      for (const auto& read : pair.second.readset()) {
        if (read.key() == write.key()) {
          Timestamp pendingTxTs(pair.second.timestamp());
          Timestamp rts(read.readtime());
          if (txTs > rts && txTs < pendingTxTs) {
            Debug("found prepared conflict with write for key: %s", write.key().c_str());
            return false;
          }
        }
      }
    }
  }
  return true;

}

::google::protobuf::Message* Server::returnMessage(::google::protobuf::Message* msg) {
  // Send decision to client
  if (signMessages) {
    Debug("Signing message");
    proto::SignedMessage *signedMessage = new proto::SignedMessage();
    SignMessage(*msg, keyManager->GetPrivateKey(id), id, *signedMessage);
    delete msg;
    return signedMessage;
  } else {
    return msg;
  }
}

std::vector<::google::protobuf::Message*> Server::Execute(const string& type, const string& msg) {
  Debug("Execute: %s", type.c_str());
  //std::unique_lock lock(atomicMutex);

  proto::Transaction transaction;
  proto::GroupedDecision gdecision;
  proto::TxnExecRequest txnExecRequest;
  if (type == transaction.GetTypeName()) {
    transaction.ParseFromString(msg);

    return HandleTransaction(transaction);
  } else if (type == gdecision.GetTypeName()) {
    gdecision.ParseFromString(msg);

    if (gdecision.status() == REPLY_FAIL) {
      std::vector<::google::protobuf::Message*> results;
      results.push_back(HandleGroupedAbortDecision(gdecision));
      return results;
    } else if(order_commit && gdecision.status() == REPLY_OK) {
      std::vector<::google::protobuf::Message*> results;
      results.push_back(HandleGroupedCommitDecision(gdecision));
      return results;
    }
    else{
      Panic("Only failed grouped decisions should be atomically broadcast");
    }
  } else if (type == txnExecRequest.GetTypeName()) {
    UW_ASSERT(execTxnServerSide);
    txnExecRequest.ParseFromString(msg);
    return ExecuteTxnServerSide(txnExecRequest);
  }
  std::vector<::google::protobuf::Message*> results;
  results.push_back(nullptr);
  return results;
}

std::vector<::google::protobuf::Message*> Server::HandleTransaction(const proto::Transaction& transaction) {
  std::unique_lock lock(atomicMutex); //TODO: might be able to make it finer.

  std::vector<::google::protobuf::Message*> results;
  proto::TransactionDecision* decision = new proto::TransactionDecision();
  //std::cerr << "allocating reply" << std::endl;

  string digest = TransactionDigest(transaction);
  Debug("Handling transaction");
  DebugHash(digest);
  stats.Increment("handle_tx",1);
  decision->set_txn_digest(digest);
  decision->set_shard_id(groupIdx);

  pendingTransactions[digest] = transaction;
  if (bufferedGDecs.find(digest) != bufferedGDecs.end()) {
    stats.Increment("used_buffered_gdec",1);
    Debug("found buffered gdecision");
    if(bufferedGDecs[digest].status() == REPLY_OK){
      //std::cerr <<" trying to call HandleGroupedCommitDecision while holding lock for txn: " << BytesToHex(digest, 16) << std::endl;
      results.push_back(HandleGroupedCommitDecision(bufferedGDecs[digest], false));
    }
    else{
      results.push_back(HandleGroupedAbortDecision(bufferedGDecs[digest]));
    }
    bufferedGDecs.erase(digest);
    return results;
  }

  //endorsement check
  if(!execTxnServerSide && !EndorsementCheck(transaction)) {
    Panic("Endorsement check failed for txn %s", TransactionDigest(transaction));
  }

  // OCC check
  if (CCC2(transaction)) {
    stats.Increment("ccc_succeed",1);
    Debug("ccc succeeded");
    decision->set_status(REPLY_OK);
    pendingTransactions[digest] = transaction;

    // update prepared reads and writes
    Timestamp txTs(transaction.timestamp());
    for (const auto& write : transaction.writeset()) {
      if(!IsKeyOwned(write.key())) {
        continue;
      }
      preparedWrites[write.key()].insert(txTs);
    }
    for (const auto& read : transaction.readset()) {
      if(!IsKeyOwned(read.key())) {
        continue;
      }
      preparedReads[read.key()][txTs] = read.readtime();
    }

    // check for buffered gdecision
    // if (bufferedGDecs.find(digest) != bufferedGDecs.end()) {
    //   stats.Increment("used_buffered_gdec",1);
    //   Debug("found buffered gdecision");
    //   results.push_back(HandleGroupedCommitDecision(bufferedGDecs[digest]));
    //   bufferedGDecs.erase(digest);
    // }

    // check if this transaction was already aborted
    if (false & abortedTxs.find(digest) != abortedTxs.end() ) { //this branch of code is not used anymore
      //it was only used for Writeback Acks...
      stats.Increment("gdec_failed_buf",1);
      // abort the tx
      cleanupPendingTx(digest);
      proto::GroupedDecisionAck* groupedDecisionAck = new proto::GroupedDecisionAck();
      groupedDecisionAck->set_status(REPLY_FAIL);
      groupedDecisionAck->set_txn_digest(digest);
      results.push_back(returnMessage(groupedDecisionAck));
    }
  } else {
    Debug("ccc failed");
    stats.Increment("ccc_fail",1);
    decision->set_status(REPLY_FAIL);
    pendingTransactions[digest] = transaction;

    // if (bufferedGDecs.find(digest) != bufferedGDecs.end()) {
    //   stats.Increment("used_buffered_gdec",1);
    //   Debug("found buffered gdecision");
    //   results.push_back(HandleGroupedAbortDecision(bufferedGDecs[digest]));
    //   bufferedGDecs.erase(digest);
    // }
  }


  results.push_back(decision);

  return results;
}

::google::protobuf::Message* Server::HandleMessage(const string& type, const string& msg) {
  Debug("Handle %s", type.c_str());
  //std::shared_lock lock(atomicMutex);
  //std::unique_lock lock(atomicMutex);

  proto::Read read;
  proto::GroupedDecision gdecision;

  if (type == read.GetTypeName()) {
    read.ParseFromString(msg);

    return HandleRead(read);
  } else if (type == gdecision.GetTypeName()) {
    if(order_commit && signMessages){
      Panic("Should be ordering all Writeback messages");
    }
    gdecision.ParseFromString(msg);
    if (gdecision.status() == REPLY_OK) {
      //std::unique_lock lock(atomicMutex);
      return HandleGroupedCommitDecision(gdecision);
    } else {
      Panic("Only commit grouped decisions allowed to be sent directly to server");
    }

  }
  else{
    Panic("Request not of type Read (or Commit Writeback)");
  }

  return nullptr;
}

::google::protobuf::Message* Server::HandleRead(const proto::Read& read) {
  Timestamp ts(read.timestamp());
  //std::shared_lock lock(atomicMutex); //come back to this: probably dont need it at all.

  stats.Increment("total_reads_processed", 1);
  pair<Timestamp, ValueAndProof> result;
  bool exists = commitStore.get(read.key(), ts, result);

  proto::ReadReply* readReply = new proto::ReadReply();
  Debug("Handle read req id %lu", read.req_id());
  readReply->set_req_id(read.req_id());
  readReply->set_key(read.key());
  if (exists) {
    Debug("Read exists f");
    Debug("Read exits for key: %s  value: %s", read.key().c_str(), result.second.value.c_str());
    readReply->set_status(REPLY_OK);
    readReply->set_value(result.second.value);
    result.first.serialize(readReply->mutable_value_timestamp());
    if (validateProofs) {
      *readReply->mutable_commit_proof() = *result.second.commitProof;
    }
  } else {
    stats.Increment("read_dne",1);
    Debug("Read does not exit for key: %s", read.key().c_str());
    readReply->set_status(REPLY_FAIL);
  }
  //lock.unlock_shared();
  return returnMessage(readReply);
}

::google::protobuf::Message* Server::HandleGroupedCommitDecision(const proto::GroupedDecision& gdecision, bool lock) {
  // proto::GroupedDecisionAck* groupedDecisionAck = new proto::GroupedDecisionAck();

  Debug("Handling Grouped commit Decision");
  string digest = gdecision.txn_digest();
  DebugHash(digest);

  //std::cerr <<" called HandleGroupedCommitDecision for txn: " << BytesToHex(digest, 16) << std::endl;
  // groupedDecisionAck->set_txn_digest(digest);
  if(lock) atomicMutex.lock();
  //std::cerr <<" acquired 1st lock in HandleGroupedcommit for txb:" << BytesToHex(digest, 16) << std::endl;
  if (pendingTransactions.find(digest) == pendingTransactions.end()) {

    Debug("Buffering gdecision");
    stats.Increment("buff_dec",1);
    // we haven't yet received the tx so buffer this gdecision until we get it
    bufferedGDecs[digest] = gdecision;
    if(lock) atomicMutex.unlock();
    //std::cerr << "buffering HandleGroupedCommitDecision for txn: " << BytesToHex(digest, 16)  << std::endl;
    return nullptr;
  }
  if(lock) atomicMutex.unlock();
  //std::cerr <<" released 1st lock in HandleGroupedcommit for txb:" << BytesToHex(digest, 16) << std::endl;
  // verify gdecision

    // struct timeval tp;
    // gettimeofday(&tp, NULL);
    // long int us = tp.tv_sec * 1000 * 1000 + tp.tv_usec;
  // skip validating GDecision if we are executing txn server side
  if (execTxnServerSide || verifyGDecision_parallel(gdecision, pendingTransactions[digest], keyManager, signMessages, config.f, tp)) {

  //if (verifyGDecision(gdecision, pendingTransactions[digest], keyManager, signMessages, config.f)) {
 //if(true){
    // gettimeofday(&tp, NULL);
    // long int lock_time = ((tp.tv_sec * 1000 * 1000 + tp.tv_usec) -us);
    // std::cerr << "Commit Verification takes " << lock_time << " microseconds" << std::endl;
     //std::cerr <<" about to acquire 2st lock in HandleGroupedcommit for txn:" << BytesToHex(digest, 16) << std::endl;
    std::unique_lock atomic_lock = lock ? std::unique_lock(atomicMutex) : std::unique_lock<std::shared_mutex>();


    //std::unique_lock lock(atomicMutex);
    stats.Increment("apply_tx",1);
    proto::Transaction txn = pendingTransactions[digest];
    Timestamp ts(txn.timestamp());
    // apply tx
    Debug("applying tx");
    for (const auto &read : txn.readset()) {
      if(!IsKeyOwned(read.key())) {
        continue;
      }
      Debug("applying read to key %s", read.key().c_str());
      committedReads[read.key()][ts] = read.readtime();
    }

    proto::CommitProof proof;
    *proof.mutable_writeback_message() = gdecision;
    *proof.mutable_txn() = txn;
    shared_ptr<proto::CommitProof> commitProofPtr = make_shared<proto::CommitProof>(move(proof));

    for (const auto &write : txn.writeset()) {
      if(!IsKeyOwned(write.key())) {
        continue;
      }

      ValueAndProof valProof;

      valProof.value = write.value();
      valProof.commitProof = commitProofPtr;
      Debug("applying write to key %s", write.key().c_str());
      commitStore.put(write.key(), valProof, ts);

      // GC stuff?
      // auto rtsItr = rts.find(write.key());
      // if (rtsItr != rts.end()) {
      //   auto itr = rtsItr->second.begin();
      //   auto endItr = rtsItr->second.upper_bound(ts);
      //   while (itr != endItr) {
      //     itr = rtsItr->second.erase(itr);
      //   }
      // }
    }

    // mark txn as commited
    cleanupPendingTx(digest);
    // groupedDecisionAck->set_status(REPLY_OK);
  } else {
    stats.Increment("gdec_failed_valid",1);
    // groupedDecisionAck->set_status(REPLY_FAIL);
  }
  //std::cerr << "finishing HandleGroupedCommitDecision normally" << BytesToHex(digest, 16) << std::endl;
  // Debug("decision ack status: %d", groupedDecisionAck->status());

  // return returnMessage(groupedDecisionAck);
  return nullptr;
}


::google::protobuf::Message* Server::HandleGroupedAbortDecision(const proto::GroupedDecision& gdecision) {
  // proto::GroupedDecisionAck* groupedDecisionAck = new proto::GroupedDecisionAck();


  Debug("Handling Grouped abort Decision");
  string digest = gdecision.txn_digest();
  DebugHash(digest);

  atomicMutex.lock();
  if (pendingTransactions.find(digest) == pendingTransactions.end()) {

    Debug("Buffering gdecision");
    stats.Increment("buff_dec",1);
    // we haven't yet received the tx so buffer this gdecision until we get it
    bufferedGDecs[digest] = gdecision;
    atomicMutex.unlock();
    return nullptr;
  }
  atomicMutex.unlock();

 if(validate_abort){
   // struct timeval tp;
   // gettimeofday(&tp, NULL);
   // long int us = tp.tv_sec * 1000 * 1000 + tp.tv_usec;

   if(!verifyG_Abort_Decision(gdecision, pendingTransactions[digest], keyManager, signMessages, config.f)){
    //if(!verifyGDecision_Abort_parallel(gdecision, pendingTransactions[digest], keyManager, signMessages, config.f, tp)){
     Debug("failed validation for abort decision");
     return nullptr;
   }

   // gettimeofday(&tp, NULL);
   // long int lock_time = ((tp.tv_sec * 1000 * 1000 + tp.tv_usec) -us);
   // std::cerr << "Abort Verification takes " << lock_time << " microseconds" << std::endl;
 }
 std::unique_lock lock(atomicMutex);

  // groupedDecisionAck->set_txn_digest(digest);

  stats.Increment("gdec_failed",1);
  // abort the tx
  cleanupPendingTx(digest);
  // there is a chance that this abort comes before we see the tx, so save the decision
  abortedTxs.insert(digest);

  // groupedDecisionAck->set_status(REPLY_FAIL);
  //
  // return returnMessage(groupedDecisionAck);
  return nullptr;
}

void Server::cleanupPendingTx(std::string digest) {
  if (pendingTransactions.find(digest) != pendingTransactions.end()) {
    proto::Transaction tx = pendingTransactions[digest];
    // remove prepared reads and writes
    Timestamp txTs(tx.timestamp());
    for (const auto& write : tx.writeset()) {
      if(!IsKeyOwned(write.key())) {
        continue;
      }
      preparedWrites[write.key()].erase(txTs);
    }
    for (const auto& read : tx.readset()) {
      if(!IsKeyOwned(read.key())) {
        continue;
      }
      preparedReads[read.key()].erase(txTs);
    }

    pendingTransactions.erase(digest);
  }
}

void Server::Load(const string &key, const string &value,
    const Timestamp timestamp) {
      // if (IsKeyOwned(key)) {
  ValueAndProof val;
  val.value = value;
  val.commitProof = dummyProof;
  commitStore.put(key, val, timestamp);

      // }
}

Stats &Server::GetStats() {
  return stats;
}

Stats* Server::mutableStats() {
  return &stats;
}

////////////////////////////////////////////
/*        SINTR SPECIFIC FUNCTIONS        */
////////////////////////////////////////////


//TODO: Move to common folder
void Server::LoadPolicyStore(const std::string &policyStorePath) {
  std::unique_ptr<PolicyCache> policies = policyParseClient.ParseConfigFile(policyStorePath);
  std::vector<std::string> policyIds = policies->GetAllKeys();

  for (const auto &p : policyIds) {
    std::unique_ptr<Policy> policy = policies->Take(p);
    policyStore.put(p, policy.get(), Timestamp());
    policiesToFree.push_back(std::move(policy));
  }
}

//TODO: Move to common folder
//RETURNS True if endorsement check passed, otherwise false
bool Server::EndorsementCheck(const proto::Transaction &txn) {

  PolicyClient policyClient;
  ExtractPolicy(txn, policyClient);
  return ValidateEndorsements(policyClient, &txn.endorsements(), txn.client_id(), TransactionDigest(txn));
}

//TODO: Move to common folder
void Server::ExtractPolicy(const proto::Transaction &txn, PolicyClient &policyClient) {
  // struct timespec ts_start;
  // clock_gettime(CLOCK_MONOTONIC, &ts_start);
  // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
  //TODO: Implement versioning for policy store
  std::unordered_set<std::string> policiesChecked;

  for (const auto &write : txn.writeset()) {
    if (write.is_table_col_version()) {
      // skip table column versions
      continue;
    }
    // Peloton doesn't support sharding, skipping IsKeyOwned check
    // if (!IsKeyOwned(write.key())) {
    //   // skip if write key is not owned
    //   continue;
    // }

    std::string policyId = policyIdFunction(write.key(), write.value());

    if (policiesChecked.find(policyId) != policiesChecked.end()) {
      continue;
    }
    else {
      policiesChecked.insert(policyId);
    }

    Debug("Extracting policy %s for key %s", policyId.c_str(), BytesToHex(write.key(), 16).c_str());

    std::pair<Timestamp, const Policy*> tsPolicy;
    policyStore.get(policyId, Timestamp(txn.timestamp()), tsPolicy);
    policyClient.AddPolicy(tsPolicy.second);
  }

  if (!sintr_params.includeReadsetForTxnPolicy && !sintr_params.checkPolicyLeak) {
    // no need to consider readset for policy or leak check
    return;
  }

  // policies from readset to add into policyClient
  std::unordered_set<const Policy *> readsetPoliciesToAdd;

  for (const auto &read : txn.readset()) {
    if (read.is_table_col_version()) {
      // skip table column versions
      continue;
    }
    // Peloton doesn't support sharding, skipping IsKeyOwned check
    // if (!IsKeyOwned(read.key())) {
    //   continue;
    // }

    std::string policyId = policyIdFunction(read.key(), "");
    Debug("Extracting policy %s for key %s", policyId.c_str(), BytesToHex(read.key(), 16).c_str());
    std::pair<Timestamp, const Policy*> tsPolicy;
    policyStore.get(policyId, Timestamp(read.readtime()), tsPolicy);

    // at least one of includeReadsetForTxnPolicy and checkPolicyLeak is true
    if (sintr_params.includeReadsetForTxnPolicy) {
      if (policiesChecked.find(policyId) == policiesChecked.end()) {
        readsetPoliciesToAdd.insert(tsPolicy.second);
        policiesChecked.insert(policyId);
      }
    }

    if (sintr_params.checkPolicyLeak) {
      // disallow readset to contain a policy that does not imply the write set policy
      if (!policyClient.IsImpliedBy(tsPolicy.second)) {
        Panic(
          "Read policy (%s) does not imply write policy (%s)",
          tsPolicy.second->ToString().c_str(),
          policyClient.ToString().c_str()
        );
      }
    }
  }

  if (sintr_params.includeReadsetForTxnPolicy) {
    for (const auto &p : readsetPoliciesToAdd) {
      policyClient.AddPolicy(p);
    }
  }

  // struct timespec ts_end;
  // clock_gettime(CLOCK_MONOTONIC, &ts_end);
  // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
  // auto duration = end - start;
  // extract_policy_us.add(duration);
}

bool Server::ValidateEndorsements(const PolicyClient &policyClient, const proto::SignedMessages *endorsements, 
    uint64_t client_id, const std::string &txnDigest) {

  // client initiating txn is always an endorser
  std::set<uint64_t> endorsers;
  endorsers.insert(client_id);

  if (endorsements != nullptr) {
    for (const auto &endorsement : endorsements->sig_msgs()) {
      if (!ValidateEndorsementHelper(endorsement, txnDigest)) {
        Debug(
          "Endorsement txn %s failed to validate endorsement from client %lu",
          BytesToHex(txnDigest, 16).c_str(),
          endorsement.replica_id()
        );
        continue;
      }
      endorsers.insert(endorsement.replica_id());
    }
  }

  // check if endorsers satisfy policy
  return policyClient.IsSatisfied(endorsers);
}



bool Server::ValidateEndorsementHelper(const proto::SignedMessage &endorsement, const std::string &txnDigest) {
  // cannot have empty data
  if (endorsement.packed_msg().length() == 0) {
    Warning("packed msg length is 0");
    return false;
  }
  // then check that data is all same as well
  if (txnDigest != endorsement.packed_msg()) {
    Warning("Mismatch in endorsements");
    Warning("Transaction digest %s is not same as endorsement msg %s", BytesToHex(txnDigest, 16).c_str(), BytesToHex(endorsement.packed_msg(), 16).c_str());
    return false;
  }

  // check signature
  if (sintr_params.signFinishValidation) {
    // struct timespec ts_start;
    // clock_gettime(CLOCK_MONOTONIC, &ts_start);
    // uint64_t start = ts_start.tv_sec * 1000 * 1000 + ts_start.tv_nsec / 1000;
    if (!CheckSignature(endorsement, keyManager, true)) {
      Warning(
        "Txn %s failed to validate endorsement from client %lu",
        BytesToHex(txnDigest, 16).c_str(),
        endorsement.replica_id()
      );
      return false;
    }
    // struct timespec ts_end;
    // clock_gettime(CLOCK_MONOTONIC, &ts_end);
    // uint64_t end = ts_end.tv_sec * 1000 * 1000 + ts_end.tv_nsec / 1000;
    // auto duration = end - start;
    // validate_endorsements_us.add(duration);
  }
  return true;
}

////////////////////////////////////////////
/*     SERVERCLIENT DIRECT-CALL METHODS   */
////////////////////////////////////////////

void Server::DirectRead(const std::string &key, const Timestamp &ts, uint64_t req_id,
    std::function<void(int, const std::string&, const std::string&, const Timestamp&)> gcb) {
  proto::Read read;
  read.set_req_id(req_id);
  read.set_key(key);
  ts.serialize(read.mutable_timestamp());
  read.set_client_id(0);

  ::google::protobuf::Message *msg = HandleRead(read);
  proto::ReadReply *reply = dynamic_cast<proto::ReadReply*>(msg);
  if (reply == nullptr) {
    // HandleRead returned a SignedMessage wrapper - unwrap it.
    // packed_msg holds a serialized PackedMessage (not a raw ReadReply).
    proto::SignedMessage *signedMsg = dynamic_cast<proto::SignedMessage*>(msg);
    if (signedMsg != nullptr) {
      proto::PackedMessage packedMsg;
      proto::ReadReply inner;
      if (packedMsg.ParseFromString(signedMsg->packed_msg()) &&
          inner.ParseFromString(packedMsg.msg())) {
        delete msg;
        Timestamp valTs;
        if (inner.status() == REPLY_OK && inner.has_value_timestamp()) {
          valTs = Timestamp(inner.value_timestamp());
        }
        gcb(inner.status(), inner.key(), inner.status() == REPLY_OK ? inner.value() : "", valTs);
        return;
      }
      delete msg;
    }
    gcb(REPLY_FAIL, key, "", Timestamp());
    return;
  }
  Timestamp valTs;
  if (reply->status() == REPLY_OK && reply->has_value_timestamp()) {
    valTs = Timestamp(reply->value_timestamp());
  }
  gcb(reply->status(), reply->key(), reply->status() == REPLY_OK ? reply->value() : "", valTs);
  delete msg;
}

transaction_status_t Server::DirectCommit(const proto::Transaction &txn) {
  // Run the CCC check and get shard decision
  std::vector<::google::protobuf::Message*> results = HandleTransaction(txn);

  transaction_status_t status = ABORTED_SYSTEM;
  for (auto *msg : results) {
    if (msg == nullptr) continue;
    proto::TransactionDecision *decision = dynamic_cast<proto::TransactionDecision*>(msg);
    if (decision != nullptr) {
      if (decision->status() == REPLY_OK) {
        status = COMMITTED;
      } else {
        status = ABORTED_SYSTEM;
        delete msg;
        break;
      }
    }
    delete msg;
  }

  if (status == COMMITTED) {
    // Build a trivial GroupedDecision to finalize the commit in the store
    std::string digest = TransactionDigest(txn);
    proto::GroupedDecision gdec;
    gdec.set_txn_digest(digest);
    gdec.set_status(REPLY_OK);
    ::google::protobuf::Message *ack = HandleGroupedCommitDecision(gdec, /*lock=*/true);
    if (ack != nullptr) delete ack;
  }

  return status;
}

std::vector<::google::protobuf::Message*> Server::ExecuteTxnServerSide(
    const proto::TxnExecRequest &req) {
  std::vector<::google::protobuf::Message*> results;

  // Lazily construct the ServerClient + SyncClient pair (once per Server).
  if (serverClientForExec == nullptr) {
    uint64_t exec_client_id = static_cast<uint64_t>(-1); // sentinel: server-side exec id
    serverClientForExec = new ServerClient(this, exec_client_id, tp);
    syncClientForExec   = new SyncClient(serverClientForExec);
  }
  Debug("Starting transaction for client %lu seq num %lu", req.client_id(), req.client_seq_num());

  // Parse the embedded TxnState to obtain the right ValidationTransaction.
  TxnState txnState;
  if (!txnState.ParseFromString(req.txn_state())) {
    Debug("ExecuteTxnServerSide: failed to parse TxnState");
    proto::TxnExecReply *reply = new proto::TxnExecReply();
    reply->set_client_id(req.client_id());
    reply->set_client_seq_num(req.client_seq_num());
    reply->set_status(static_cast<int32_t>(ABORTED_SYSTEM));
    results.push_back(reply);
    return results;
  }

  // ValidationParseClient is stateless (timeout only) – cheap to construct.
  ValidationParseClient parseClient(/*timeout=*/5000);
  ValidationTransaction *valTxn = parseClient.Parse(txnState);
  if (valTxn == nullptr) {
    Warning("ExecuteTxnServerSide: ParseClient returned nullptr for txn_name=%s",
            txnState.txn_name().c_str());
    proto::TxnExecReply *reply = new proto::TxnExecReply();
    reply->set_client_id(req.client_id());
    reply->set_client_seq_num(req.client_seq_num());
    reply->set_status(static_cast<int32_t>(ABORTED_SYSTEM));
    results.push_back(reply);
    return results;
  }

  // Run the transaction synchronously via SyncClient (which wraps ServerClient,
  // which calls DirectRead / DirectCommit directly on this Server).
  transaction_status_t status = valTxn->Validate(*syncClientForExec);
  delete valTxn;
  Debug("status is %d", static_cast<int32_t>(status));

  proto::TxnExecReply *reply = new proto::TxnExecReply();
  reply->set_client_id(req.client_id());
  reply->set_client_seq_num(req.client_seq_num());
  reply->set_status(static_cast<int32_t>(status));
  Debug("Successfully executed txn for client %lu seq num %lu", req.client_id(), req.client_seq_num());
  results.push_back(reply);
  return results;
}

}
