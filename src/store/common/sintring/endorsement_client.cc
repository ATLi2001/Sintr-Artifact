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

#include "store/common/sintring/endorsement_client.h"
#include "store/common/util.h"
#include "lib/message.h"
#include "lib/assert.h"

#include <algorithm>


EndorsementClient::EndorsementClient(uint64_t client_id) : client_id(client_id) {}
EndorsementClient::~EndorsementClient() {}

const std::vector<std::shared_ptr<::google::protobuf::Message>> &EndorsementClient::GetEndorsements() const {
  endorsementCheckStatesMap::const_accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called from the client main thread before the txn is sent to the server
    // so the endorsement check state should always exist
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }
  return *a->second->endorsements;
}

const std::set<uint64_t> &EndorsementClient::GetBlacklistedClients() const {
  return blacklistedClients;
}

void EndorsementClient::SetClientSeqNum(uint64_t client_seq_num) {
  this->client_seq_num = client_seq_num;
  endorsementCheckStatesMap::accessor a;
  const bool isNewKey = endorsementCheckStates.insert(a, client_seq_num);
  if (!isNewKey) {
    Panic("SetClientSeqNum called with existing client seq num %lu", client_seq_num);
  }
  a->second = new EndorsementCheckState();
}

void EndorsementClient::SetEndorsementsUsed() {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called from client main thread Commit() before the txn is sent to the server
    // so the endorsement check state should always exist
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }
  a->second->endorsementsUsed = true;
  if (a->second->Done()) {
    Debug("Removing endorsement check state for client seq num %lu", client_seq_num);
    endorsementCheckStates.erase(a);
  }
}

void EndorsementClient::SetExpectedTxnDigest(const std::string &expectedTxnDigest) {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called from client main thread Commit() before the txn is sent to the server
    // so the endorsement check state should always exist
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }

  a->second->expectedTxnDigest = expectedTxnDigest;
  // add self as an endorsement
  a->second->client_ids_received.insert(client_id);

  // now also check pendingEndorsements
  for (auto const &it : a->second->pendingEndorsements) {
    if (expectedTxnDigest == it.second.first) {
      a->second->client_ids_received.insert(it.first);
      a->second->endorsements->push_back(it.second.second);
    }
    else {
      Debug(
        "No match on pending endorsement from client id %lu, txn digest %s; expected txn digest %s",
        it.first,
        BytesToHex(it.second.first, 16).c_str(),
        BytesToHex(expectedTxnDigest, 16).c_str()
      );
      blacklistedClients.insert(it.first);
    }
    a->second->numCheckValidations++;
  }
  for (auto const &it : a->second->pendingDigests) {
    if (expectedTxnDigest != it.second) {
      Debug(
        "No match on pending digest from client id %lu, txn digest %s; expected txn digest %s",
        it.first,
        BytesToHex(it.second, 16).c_str(),
        BytesToHex(expectedTxnDigest, 16).c_str()
      );
      blacklistedClients.insert(it.first);
    }
    a->second->numCheckValidations++;
  }

  a->second->pendingEndorsements.clear();
  a->second->pendingDigests.clear();

  if (a->second->Done()) {
    // if we have checked enough validations, we can destruct the endorsement check state
    endorsementCheckStates.erase(a);
  }
}

void EndorsementClient::DebugSetExpectedTxn(std::unique_ptr<::google::protobuf::Message> expectedTxn) {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }

  // Debug(
  //   "DebugSetExpectedTxn for EndorsementClient client id %lu, seq num %lu",
  //   expectedTxn.client_id(),
  //   expectedTxn.client_seq_num()
  // );

  for (auto const &txn : a->second->pendingTxns) {
    DebugCheckFunction(expectedTxn.get(), txn.get());
  }
  a->second->expectedTxn = std::move(expectedTxn);
  a->second->pendingTxns.clear();
}

void EndorsementClient::DebugCheck(std::unique_ptr<::google::protobuf::Message> txn) {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    Debug("No endorsement check state found for client seq num %lu", client_seq_num);
    return;
  }
  const ::google::protobuf::Message *expectedTxn = a->second->expectedTxn.get();
  if (expectedTxn == nullptr) {
    a->second->pendingTxns.push_back(std::move(txn));
    return;
  }
  DebugCheckFunction(expectedTxn, txn.get());
}

void EndorsementClient::UpdateRequirement(const Policy *policy) {
  UW_ASSERT(policy != nullptr);
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called from client main thread before the txn is sent to the server
    // so the endorsement check state should always exist
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }
  a->second->policyClient->AddPolicy(policy);
}

std::vector<int> EndorsementClient::DifferenceToSatisfied(const std::set<uint64_t> &potentialEndorsements) const {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this could happen if transaction completes while this was scheduled off critical path
    // and the update requirement did not end up affecting the policy
    Debug("No endorsement check state found for client seq num %lu", client_seq_num);
    return {};
  }
  return a->second->policyClient->DifferenceToSatisfied(potentialEndorsements);
}

void EndorsementClient::AddValidation(const uint64_t peer_client_id, const std::string &valTxnDigest,
    std::shared_ptr<::google::protobuf::Message> signedValTxnDigest) {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called in the non-optimistic case but could be from a signature check thread
    // it is possible that while the signature check is happening, the client has moved on to the next txn
    // and enough overall checks have been completed, so the endorsement check state has been removed
    Debug("No endorsement check state found for client seq num %lu", client_seq_num);
    return;
  }
  std::set<uint64_t> &client_ids_received = a->second->client_ids_received;
  const std::string &expectedTxnDigest = a->second->expectedTxnDigest;
  // if new peer
  if (client_ids_received.find(peer_client_id) == client_ids_received.end()) {
    if (expectedTxnDigest.length() > 0) {
      // must match expected digest
      if (valTxnDigest == expectedTxnDigest) {
        client_ids_received.insert(peer_client_id);
        a->second->endorsements->push_back(signedValTxnDigest);
      }
      else {
        Debug(
          "No match on endorsement from client id %lu, txn digest %s; expected txn digest %s",
          peer_client_id,
          BytesToHex(valTxnDigest, 16).c_str(),
          BytesToHex(expectedTxnDigest, 16).c_str()
        );
        blacklistedClients.insert(peer_client_id);
      }
      a->second->numCheckValidations++;
    }
    else {
      // possible for expected digest to be uninitialized, in which case record a pending endorsement
      a->second->pendingEndorsements[peer_client_id] = std::make_pair(valTxnDigest, signedValTxnDigest);
      Debug("No expectedTxnDigest yet");
    }
  }
  if (a->second->Done()) {
    endorsementCheckStates.erase(a);
  }
}

void EndorsementClient::AddValidationOptimistic(const uint64_t peer_client_id, std::shared_ptr<::google::protobuf::Message> signedValTxnDigest) {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called in the optimistic case but on the main thread
    // and before this is called the seq num is checked to be stale
    // so here the endorsement check state should always exist
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }
  // if new peer
  if (a->second->client_ids_received.find(peer_client_id) == a->second->client_ids_received.end()) {
    Debug("Adding optimistic validation from peer client %lu, seq num %lu", peer_client_id, client_seq_num);
    a->second->client_ids_received.insert(peer_client_id);
    a->second->endorsements->push_back(signedValTxnDigest);
  }
  else {
    Debug("Client %lu already has endorsement from peer client %lu", client_id, peer_client_id);
  }
}

void EndorsementClient::CheckValidation(const uint64_t peer_client_id, uint64_t client_seq_num, const std::string &valTxnDigest) {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // CheckValidation() is called in the optimistic case on a signature check thread
    // it is possible that while the signature check is happening, the client has moved on to the next txn
    // and enough overall checks have been completed, so the endorsement check state has been removed
    Debug("No endorsement check state found for client seq num %lu", client_seq_num);
    return;
  }
  const std::string &expectedTxnDigest = a->second->expectedTxnDigest;
  if (expectedTxnDigest.length() > 0) {
    if (valTxnDigest != expectedTxnDigest) {
      Debug(
        "Validation digest %s does not match expected digest %s for peer client %lu",
        BytesToHex(valTxnDigest, 16).c_str(),
        BytesToHex(expectedTxnDigest, 16).c_str(),
        peer_client_id
      );
      // add to blacklist
      blacklistedClients.insert(peer_client_id);
    }
    a->second->numCheckValidations++;
  }
  else {
    a->second->pendingDigests[peer_client_id] = valTxnDigest;
    Debug("No expectedTxnDigest yet for client seq num %lu", client_seq_num);
  }
  if (a->second->Done()) {
    endorsementCheckStates.erase(a);
  }
}

bool EndorsementClient::IsSatisfied() {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called from the client main thread before the txn is sent to the server
    // so the endorsement check state should always exist
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }
  bool satisfied = a->second->policyClient->IsSatisfied(a->second->client_ids_received);
  if (!satisfied) {
    // Debug("policy not satisfied, received %lu endorsements", client_ids_received.size());
  }
  else {
    // -1 for self
    a->second->numChecksBeforeDestruct = a->second->client_ids_received.size() - 1;
  }
  return satisfied;
}

void EndorsementClient::SetDebugCheckFunction(std::function<void(const ::google::protobuf::Message *expectedTxn, const ::google::protobuf::Message *txn)> func) {
  DebugCheckFunction = func;
}
