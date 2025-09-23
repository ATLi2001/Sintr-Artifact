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

#ifndef SINTRING_PARAMS_H
#define SINTRING_PARAMS_H

#include "lib/message.h"
#include <string>

enum CLIENT_VALIDATION_HEURISTIC {
  EXACT, // contact exactly the number of clients as estimated
  ONE_MORE, // contact one more client than estimated
  ALL // contact all clients
};

// Sintr protocol specific parameters
typedef struct SintrParameters {
  const uint64_t maxValThreads; // maximum number of validation threads
  const bool signFwdReadResults; // sign (and validate) forward read results
  const bool signFinishValidation; // sign (and validate) finish validation messages
  const bool debugEndorseCheck; // debug endorsement check
  const bool clientCheckEvidence; // client checks prepared and committed evidence on forward read results
  const std::string policyFunctionName; // name of the policy function to use
  const std::string policyConfigPath; // path to the policy configuration file
  const uint32_t readIncludePolicy; // period of include policy in read messages
  const uint64_t minEnablePullPolicies;
  // heuristic from estimate to actual number of clients contacted for validation
  const CLIENT_VALIDATION_HEURISTIC clientValidationHeuristic;
  const bool checkPolicyLeak;
  const bool clientPinCores; // pin client cores for validation
  const bool c2cSendThread; // separate thread for sending client-to-client communication
  const bool c2cReceiveThread; // separate thread for receiving client-to-client communication
  const bool parallelEndorsementCheck; // parallel endorsement check
  const bool useOCCForPolicies; // use OCC for policies, changing policies in sql means this flag must be enabled
  const bool hashEndorsements; // hash endorsements with txn digest to get updated txn digest
  const bool parallelQuerySigsCheck; // parallel query signature check on forwarded query results
  const bool blindWriteMessage; // send a blind write message to validating clients
  const bool sortWriteset; // sort write set in order to get endorsement matches
  const bool hideTimestamps; // do not send timestamp information to validation clients if true
  const uint32_t maxClientSigCheckThreads; // maximum number of parallel client signature check threads
  const bool serverSkipEndorsementCheck; // server skips endorsement check completely
  const bool policyCCC; // perform CCC on policies
  const bool optimisticReceiveEndorsement; // receive endorsements optimistically (i.e. do not check for endorsement correctness before attempting to commit)
  const bool ignorePolicyUpdate; // ignore policy updates during a transaction
  const bool clientEstimatePolicy; // client estimates policy at start of transaction
  const bool hashQueryGenId; // hash query general id
  const bool separateTransport; // enable separate transport object for c2c communication
  const uint32_t maxClientsConnect; // max number of clients that a single client connects to
  const bool c2cUseAsynchVal; // use asynch validation for transaction writes in C2C
  const bool useEndorsementCB; // use callback function instead of busy waiting for endorsements

  SintrParameters(uint64_t maxValThreads, bool signFwdReadResults, bool signFinishValidation,
    bool debugEndorseCheck, bool clientCheckEvidence, std::string policyFunctionName,
    std::string policyConfigPath, uint32_t readIncludePolicy, CLIENT_VALIDATION_HEURISTIC clientValidationHeuristic,
    bool checkPolicyLeak, bool clientPinCores, uint64_t minEnablePullPolicies, bool c2cSendThread, bool c2cReceiveThread,
    bool parallelEndorsementCheck, bool useOCCForPolicies, bool hashEndorsements, bool parallelQuerySigsCheck,
    bool blindWriteMessage, bool sortWriteset, bool hideTimestamps, uint32_t maxClientSigCheckThreads,
    bool serverSkipEndorsementCheck, bool policyCCC, bool optimisticReceiveEndorsement, bool ignorePolicyUpdate,
    bool clientEstimatePolicy, bool hashQueryGenId, bool separateTransport, uint32_t maxClientsConnect,
    bool c2cUseAsynchVal, bool useEndorsementCB) :
    maxValThreads(maxValThreads),
    signFwdReadResults(signFwdReadResults),
    signFinishValidation(signFinishValidation),
    debugEndorseCheck(debugEndorseCheck),
    clientCheckEvidence(clientCheckEvidence),
    policyFunctionName(policyFunctionName),
    policyConfigPath(policyConfigPath),
    readIncludePolicy(readIncludePolicy),
    clientValidationHeuristic(clientValidationHeuristic),
    checkPolicyLeak(checkPolicyLeak),
    clientPinCores(clientPinCores),
    minEnablePullPolicies(minEnablePullPolicies),
    c2cSendThread(c2cSendThread),
    c2cReceiveThread(c2cReceiveThread),
    parallelEndorsementCheck(parallelEndorsementCheck),
    useOCCForPolicies(useOCCForPolicies),
    hashEndorsements(hashEndorsements),
    parallelQuerySigsCheck(parallelQuerySigsCheck),
    blindWriteMessage(blindWriteMessage),
    sortWriteset(sortWriteset),
    hideTimestamps(hideTimestamps) ,
    maxClientSigCheckThreads(maxClientSigCheckThreads),
    serverSkipEndorsementCheck(serverSkipEndorsementCheck),
    policyCCC(policyCCC),
    optimisticReceiveEndorsement(optimisticReceiveEndorsement),
    ignorePolicyUpdate(ignorePolicyUpdate),
    clientEstimatePolicy(clientEstimatePolicy),
    hashQueryGenId(hashQueryGenId),
    separateTransport(separateTransport),
    maxClientsConnect(maxClientsConnect),
    c2cUseAsynchVal(c2cUseAsynchVal),
    useEndorsementCB(useEndorsementCB)
     {
        // either sort write set or send blind write message to get endorsement matches
        // doing neither will result in potential endorsement mismatch from nondeterministic write set ordering
        // potential optimization: don't sort writeset unless there is a blind write message
        if(!sortWriteset && !blindWriteMessage) {
            Warning("Neither sortWriteset nor blindWriteMessage is enabled. This may lead to endorsement mismatch due to nondeterministic write set ordering.");
        }

        if (parallelEndorsementCheck || parallelQuerySigsCheck) {
            if (maxClientSigCheckThreads == 0) {
                Warning("Parallel endorsement check or query signature check is enabled, but maxClientSigCheckThreads is set to 0.");
            }
        }

        if (optimisticReceiveEndorsement) {
            // only makes sense if parallel endorsement check is on and actual signatures to validate
            if (!parallelEndorsementCheck || !signFinishValidation) {
                Warning("Optimistic receive endorsement is enabled, but parallel endorsement check or signFinishValidation is not.");
            }
        }

        if(maxClientsConnect > 0 && !separateTransport) {
            Warning("Max clients connect parameter is greater than 0, but separate transport is not enabled");
        }
    }

} SintrParameters;

#endif /* SINTRING_PARAMS_H */
