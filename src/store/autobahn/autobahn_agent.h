/***********************************************************************
 *
 * Copyright 2025 Austin Li <atl63@cornell.edu>
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
#ifndef _AUTOBAHN_AGENT_H_
#define _AUTOBAHN_AGENT_H_

// lib.rs.h cpp rust interface for autobahn
#include "bftinterface/src/lib.rs.h"
#include "lib/transport.h"
#include "lib/tcptransport.h"

namespace autobahn {

class AutobahnAgent{
public:
  AutobahnAgent(size_t id, bool is_client, TransportReceiver *receiver, TCPTransport *tcp_transport,
    const std::string &config_path, const std::string &params_file = "", bool send_to_all = true);

  void SendMessageToGroup(int group_idx, void *buffer, size_t size);
  void SetClientSeqNum(uint64_t seq_num);

private:
  void CreateClientInterface();
  void CreateServerInterface(TransportReceiver *receiver, const std::string &params_file);
  // use tcp transport lookup address functionality
  std::string GetSocketAddr(const std::string &host_port);

  const std::string committee_hostname_filename = ".committee-hostname.json";

  size_t id;
  bool is_client;
  TransportReceiver *receiver;
  TCPTransport *tcp_transport;
  std::string config_path;
  bool send_to_all;
  uint64_t client_seq_num;

  std::unique_ptr<rust::Box<AutobahnClient>> client;
  std::vector<std::unique_ptr<rust::Box<AutobahnClient>>> clients;
  std::unique_ptr<rust::Box<AutobahnServer>> primary;
  std::vector<std::unique_ptr<rust::Box<AutobahnServer>>> workers;
};

} // namespace autobahn

#endif
