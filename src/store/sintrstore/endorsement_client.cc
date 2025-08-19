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

#include "store/sintrstore/endorsement_client.h"
#include "store/sintrstore/common.h"
#include "lib/message.h"

#include <algorithm>
#include <google/protobuf/util/message_differencer.h>

namespace sintrstore {

EndorsementClient::EndorsementClient(uint64_t client_id, KeyManager *keyManager) : 
    client_id(client_id), keyManager(keyManager) {}
EndorsementClient::~EndorsementClient() {}

const std::vector<proto::SignedMessage> &EndorsementClient::GetEndorsements() const {
  endorsementCheckStatesMap::const_accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called from the client main thread before the txn is sent to the server
    // so the endorsement check state should always exist
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }
  return *a->second->endorsements;
}

std::vector<proto::SignedMessage> *EndorsementClient::ReleaseEndorsements() {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    // this is called from the client main thread before the txn is sent to the server
    // so the endorsement check state should always exist
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }
  std::vector<proto::SignedMessage> *endorsements = a->second->endorsements;
  a->second->endorsements = nullptr;
  return endorsements;
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

void EndorsementClient::DebugSetExpectedTxn(const proto::Transaction &expectedTxn) {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    Panic("No endorsement check state found for client seq num %lu", client_seq_num);
  }
  a->second->expectedTxn = expectedTxn;

  Debug(
    "DebugSetExpectedTxn for EndorsementClient client id %lu, seq num %lu",
    expectedTxn.client_id(),
    expectedTxn.client_seq_num()
  );

  for (auto const &txn : a->second->pendingTxns) {
    DebugCheck(expectedTxn, txn);
  }
  a->second->pendingTxns.clear();
}

void EndorsementClient::DebugCheck(const proto::Transaction &txn) {
  endorsementCheckStatesMap::accessor a;
  if (!endorsementCheckStates.find(a, client_seq_num)) {
    Debug("No endorsement check state found for client seq num %lu", client_seq_num);
    return;
  }
  const proto::Transaction &expectedTxn = a->second->expectedTxn;
  if (!expectedTxn.IsInitialized()) {
    a->second->pendingTxns.push_back(txn);
    return;
  }
  DebugCheck(expectedTxn, txn);
}

void EndorsementClient::DebugCheck(const proto::Transaction &expectedTxn, const proto::Transaction &txn) {
  Debug(
    "DebugCheck for EndorsementClient client id %lu, seq num %lu",
    expectedTxn.client_id(),
    expectedTxn.client_seq_num()
  );

  if (txn.client_id() != expectedTxn.client_id()) {
    Debug("client id mismatch: received %lu, expected %lu", txn.client_id(), expectedTxn.client_id());
  }

  if (txn.client_seq_num() != expectedTxn.client_seq_num()) {
    Debug("client seq num mismatch: received %lu, expected %lu", txn.client_seq_num(), expectedTxn.client_seq_num());
  }

  if (txn.involved_groups_size() != expectedTxn.involved_groups_size()) {
    Debug("involved groups mismatch: received size %d, expected size %d", txn.involved_groups_size(), expectedTxn.involved_groups_size());
  }
  for (int i = 0; i < expectedTxn.involved_groups_size(); i++) {
    if (txn.involved_groups(i) != expectedTxn.involved_groups(i)) {
      Debug("involved groups mismatch: received group %ld, expected group %ld", txn.involved_groups(i), expectedTxn.involved_groups(i));
    }
  }

  if (txn.read_set_size() != expectedTxn.read_set_size()) {
    Debug("read set mismatch: received size %d, expected size %d", txn.read_set_size(), expectedTxn.read_set_size());
  }
  for (int i = 0; i < expectedTxn.read_set_size(); i++) {
    if (!google::protobuf::util::MessageDifferencer::Equals(txn.read_set(i), expectedTxn.read_set(i))) {
      Debug(
        "read set mismatch: received key %s, ts %lu.%lu, expected key %s, ts %lu.%lu",
        txn.read_set(i).key().c_str(),
        txn.read_set(i).readtime().timestamp(),
        txn.read_set(i).readtime().id(),
        expectedTxn.read_set(i).key().c_str(),
        expectedTxn.read_set(i).readtime().timestamp(),
        expectedTxn.read_set(i).readtime().id()
      );
    }
  }

  if (txn.write_set_size() != expectedTxn.write_set_size()) {
    Debug("write set mismatch: received size %d, expected size %d", txn.write_set_size(), expectedTxn.write_set_size());
  }
  for (int i = 0; i < expectedTxn.write_set_size(); i++) {
    if (txn.write_set(i).key() != expectedTxn.write_set(i).key()) {
      Debug(
        "write set mismatch[%d]: received key %s, expected key %s",
        i,
        txn.write_set(i).key().c_str(),
        expectedTxn.write_set(i).key().c_str()
      );
    }
    if (txn.write_set(i).value() != expectedTxn.write_set(i).value()) {
      Debug(
        "write set mismatch[%d]: received value %s, expected value %s",
        i,
        BytesToHex(txn.write_set(i).value(), 16).c_str(),
        BytesToHex(expectedTxn.write_set(i).value(), 16).c_str()
      );
    }
    // if (!google::protobuf::util::MessageDifferencer::Equals(txn.write_set(i), expectedTxn.write_set(i))) {
    //   Debug(
    //     "write set mismatch: received %s, expected %s",
    //     txn.write_set(i).ShortDebugString().c_str(),
    //     expectedTxn.write_set(i).ShortDebugString().c_str()
    //   );
    // }
  }

  if (txn.deps_size() != expectedTxn.deps_size()) {
    Debug("dependencies mismatch: received size %d, expected size %d", txn.deps_size(), expectedTxn.deps_size());
  }
  for (int i = 0; i < expectedTxn.deps_size(); i++) {
    if (txn.deps(i).write().prepared_txn_digest() != expectedTxn.deps(i).write().prepared_txn_digest()) {
      Debug("dependencies mismatch: index %d", i);
    }
  }

  if (!google::protobuf::util::MessageDifferencer::Equals(txn.timestamp(), expectedTxn.timestamp())) {
    Debug(
      "timestamp mismatch: received %lu.%lu, expected %lu.%lu",
      txn.timestamp().timestamp(),
      txn.timestamp().id(),
      expectedTxn.timestamp().timestamp(),
      expectedTxn.timestamp().id()
    );
  }

  // query stuff
  if (txn.query_set_size() != expectedTxn.query_set_size()) {
    Debug("query set mismatch: received size %d, expected size %d", txn.query_set_size(), expectedTxn.query_set_size());
  }
  for (int i = 0; i < expectedTxn.query_set_size(); i++) {
    if (!google::protobuf::util::MessageDifferencer::Equals(txn.query_set(i), expectedTxn.query_set(i))) {
      Debug(
        "query set mismatch: received id %s, expected id %s",
        BytesToHex(txn.query_set(i).query_id(), 16).c_str(),
        BytesToHex(txn.query_set(i).query_id(), 16).c_str()
      );
      Debug(
        "query set mismatch: received %s, expected %s",
        txn.query_set(i).ShortDebugString().c_str(),
        expectedTxn.query_set(i).ShortDebugString().c_str()
      );
    }
  }

  if (txn.read_predicates_size() != expectedTxn.read_predicates_size()) {
    Debug("read predicates mismatch: received size %d, expected size %d", txn.read_predicates_size(), expectedTxn.read_predicates_size());
  }
  for (int i = 0; i < expectedTxn.read_predicates_size(); i++) {
    if (!google::protobuf::util::MessageDifferencer::Equals(txn.read_predicates(i), expectedTxn.read_predicates(i))) {
      Debug("read predicates mismatch: on index %d", i);
    }
  }

  // protobuf map has undefined order, so must sort first
  std::vector<std::pair<const std::string*, const TableWrite*>> tt;
  for (const auto &[table, table_write]: txn.table_writes()) {
    tt.emplace_back(&table, &table_write);
  }
  std::sort(tt.begin(), tt.end(), [](auto l, auto r){ return (*l.first) < (*r.first); });

  std::vector<std::pair<const std::string*, const TableWrite*>> expectedTt;
  for (const auto &[table, table_write]: expectedTxn.table_writes()) {
    expectedTt.emplace_back(&table, &table_write);
  }
  std::sort(expectedTt.begin(), expectedTt.end(), [](auto l, auto r){ return (*l.first) < (*r.first); });

  if (tt.size() != expectedTt.size()) {
    Debug("table writes mismatch: received size %d, expected size %d", tt.size(), expectedTt.size());
  }
  for (int i = 0; i < expectedTt.size(); i++) {
    if (*tt[i].first != *expectedTt[i].first) {
      Debug(
        "table writes mismatch: received table %s, expected table %s",
        (*tt[i].first).c_str(),
        (*expectedTt[i].first).c_str()
      );
    }
    if (tt[i].second->rows_size() != expectedTt[i].second->rows_size()) {
      Debug(
        "table writes mismatch: received rows size %d, expected rows size %d",
        tt[i].second->rows_size(),
        expectedTt[i].second->rows_size()
      );
    }

    std::vector<const RowUpdates*> rows;
    for (int j = 0; j < tt[i].second->rows_size(); j++) {
      rows.push_back(&tt[i].second->rows(j));
    }

    std::vector<const RowUpdates*> expectedRows;
    for (int j = 0; j < expectedTt[i].second->rows_size(); j++) {
      expectedRows.push_back(&expectedTt[i].second->rows(j));
    }

    for (int j = 0; j < expectedRows.size(); j++) {
      if (rows[j]->has_deletion() != expectedRows[j]->has_deletion()) {
        Debug(
          "table writes mismatch: received deletion %d, expected deletion %d",
          rows[j]->has_deletion(),
          expectedRows[j]->has_deletion()
        );
      }
      if (rows[j]->column_values_size() != expectedRows[j]->column_values_size()) {
        Debug(
          "table writes mismatch: received column values size %d, expected column values size %d",
          rows[j]->column_values_size(),
          expectedRows[j]->column_values_size()
        );
      }
      for (int k = 0; k < expectedRows[j]->column_values_size(); k++) {
        if (rows[j]->column_values(k) != expectedRows[j]->column_values(k)) {
          Debug(
            "table writes mismatch[%d][%d][%d]: received column value %s, expected column value %s",
            i, j, k,
            rows[j]->column_values(k).c_str(),
            expectedRows[j]->column_values(k).c_str()
          );
        }
      }
    }
  }
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
    const proto::SignedMessage &signedValTxnDigest) {
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

void EndorsementClient::AddValidationOptimistic(const uint64_t peer_client_id, const proto::SignedMessage &signedValTxnDigest) {
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

} // namespace sintrstore
