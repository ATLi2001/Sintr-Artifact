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

#ifndef _ENDORSEMENT_CLIENT_H_
#define _ENDORSEMENT_CLIENT_H_

#include "store/common/policy/policy_client.h"

#include <vector>
#include <set>
#include <map>
#include <shared_mutex>
#include <google/protobuf/message.h>

#include "tbb/concurrent_hash_map.h"


// this class keeps state for an ongoing transaction endorsement
// now as a generic not store specific class, endorsements are treated as generic protobuf messages
// user of this class is responsible for casting appropriately on the outside
class EndorsementClient {
 public:
  EndorsementClient(uint64_t client_id);
  ~EndorsementClient();

  const std::vector<std::shared_ptr<::google::protobuf::Message>> &GetEndorsements(const uint64_t sequence_number = -1) const;
  const std::set<uint64_t> &GetBlacklistedClients() const;
  void SetClientSeqNum(uint64_t client_seq_num);
  void SetEndorsementsUsed(const uint64_t sequence_number = -1);
  void SetExpectedTxnDigest(const std::string &expectedTxnDigest, const uint64_t sequence_number = -1);
  void DebugSetExpectedTxn(std::unique_ptr<::google::protobuf::Message> expectedTxn);
  void DebugCheck(std::unique_ptr<::google::protobuf::Message> txn);
  // update current policy by merging with passed in policy
  void UpdateRequirement(const Policy *policy, const uint64_t sequence_number = -1);
  // what additional client ids are needed so that this policy is satisfied by potentialEndorsements
  // if potentialEndorsements is good enough, return empty vector
  std::vector<int> DifferenceToSatisfied(const std::set<uint64_t> &potentialEndorsements, const uint64_t sequence_number = -1) const;
  // add validation from peer client
  // this can be called from a different thread than the rest of the functions
  void AddValidation(const uint64_t peer_client_id, const std::string &valTxnDigest, 
    std::shared_ptr<::google::protobuf::Message> signedValTxnDigest, const uint64_t sequence_number = -1);
  // in optimistic case do not check if endorsement is correct, just add it
  void AddValidationOptimistic(const uint64_t peer_client_id, std::shared_ptr<::google::protobuf::Message> signedValTxnDigest, const uint64_t sequence_number = -1);
  // check if the validation is correct, do not add it as an endorsement
  // this is used in optimistic case where endorsement is already added and we need to later check if correct
  void CheckValidation(const uint64_t peer_client_id, uint64_t client_seq_num, const std::string &valTxnDigest);
  // check if the policy is satisfied by actual endorsements collected so far
  bool IsSatisfied(const uint64_t sequence_number = -1);
  // function passed in by specific store defining its own debug check function
  void SetDebugCheckFunction(std::function<void(const ::google::protobuf::Message *, const ::google::protobuf::Message *)> func);

 private:
  // this client information
  const uint64_t client_id;
  // current sequence number for transaction that is ongoing
  uint64_t client_seq_num;

  std::function<void(const ::google::protobuf::Message *, const ::google::protobuf::Message *)> DebugCheckFunction;

  // in the optimistic case, the endorsement check state may need to stay alive even after current transaction commits
  // this is because the checking of the validity of the endorsement is off the critical path
  // so next transaction may start before that check completes
  struct EndorsementCheckState {
    EndorsementCheckState() : policyClient(new PolicyClient()),
      endorsements(new std::vector<std::shared_ptr<::google::protobuf::Message>>()) {}
    ~EndorsementCheckState() {
      delete policyClient;
      if (endorsements != nullptr) {
        delete endorsements;
      }
    }
    // ready to destruct
    bool Done() const {
      return numChecksBeforeDestruct >= 0 && numCheckValidations >= numChecksBeforeDestruct && endorsementsUsed;
    }
    // expected validation transaction digest
    std::string expectedTxnDigest;
    // debug by checking entire validation txn
    std::unique_ptr<::google::protobuf::Message> expectedTxn;
    // policy client tracks the policy which must be satisfied
    PolicyClient *policyClient;
    // which peer clients have endorsed
    std::set<uint64_t> client_ids_received;
    // confirmed endorsement signatures to send to server
    std::vector<std::shared_ptr<::google::protobuf::Message>> *endorsements;
    // also maintain pending endorsements if endorsement comes back before expectedValTxnDigest is set
    // map from client id to (digest, signed message)
    std::map<uint64_t, std::pair<std::string, std::shared_ptr<::google::protobuf::Message>>> pendingEndorsements;
    // for the optimistic case we don't actually need the endorsement, just the digest
    std::map<uint64_t, std::string> pendingDigests;
    // debug pending transactions
    std::vector<std::unique_ptr<::google::protobuf::Message>> pendingTxns;

    // deletion of an endorsement check state should happen only after:
    // 1. endorsements have been checked
    // 2. the endorsements have been used (copied into proto message to send to server)
    // in normal mode, 1. must happen before 2.
    // in optimistic mode, 2. may happen before 1.
    // thus, we need extra variables to track when both are done so we can destruct

    // number of pendingDigests we should expect to check before garbage collection
    int numChecksBeforeDestruct = -1;
    // number of validations that have been checked for this endorsement check state
    int numCheckValidations = 0;
    // is client done using the endorsements
    bool endorsementsUsed = false;
  };
  typedef tbb::concurrent_hash_map<uint64_t, EndorsementCheckState *> endorsementCheckStatesMap;
  endorsementCheckStatesMap endorsementCheckStates;

  // blacklist of clients that have sent an incorrect endorsement
  std::set<uint64_t> blacklistedClients;
};

#endif /* _ENDORSEMENT_CLIENT_H_ */
