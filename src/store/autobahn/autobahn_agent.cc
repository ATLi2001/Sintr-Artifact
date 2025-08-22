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

#include "store/autobahn/autobahn_agent.h"

namespace autobahn {

AutobahnAgent::AutobahnAgent(size_t id, bool is_client, TransportReceiver *receiver, const std::string &config_path)
    : id(id), is_client(is_client), receiver(receiver), config_path(config_path) {
  if (is_client) {
    CreateClientInterface();
  }
  else {
    CreateServerInterface(receiver);
  }
}

void AutobahnAgent::CreateClientInterface() {
  // TODO: read from json for config
  std::string target = "127.0.0.1:3000";
  size_t size = 32;
  client = std::make_unique<rust::Box<Client>>(new_client(target, size));
}

void AutobahnAgent::CreateServerInterface(TransportReceiver *receiver) {
  // // TODO: get json configs
  // std::string address = "127.0.0.1:3000";
  // size_t size = 32;
  // int64_t handle = reinterpret_cast<int64_t>(receiver);
  // server = std::make_unique<rust::Box<Server>>(
  //   new_server(handle, "key", "committee", "params", "store", 0)
  // );
}

} // namespace autobahn
