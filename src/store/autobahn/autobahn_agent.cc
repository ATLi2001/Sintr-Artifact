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
#include "lib/assert.h"
#include "lib/message.h"
#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

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
  std::filesystem::path committee_json_path = config_path;
  committee_json_path /= committee_hostname_filename;
  std::ifstream f(committee_json_path);
  json committee_json = json::parse(f);

  std::vector<std::string> authorities;
  std::vector<size_t> num_workers_per_authority;
  for (auto& [key, value] : committee_json["authorities"].items()) {
    authorities.push_back(key);
    num_workers_per_authority.push_back(value["workers"].size());
    Debug("Authority: %s", key.c_str());
  }

  // distribute clients evenly across authorities
  size_t target_authority = id % authorities.size();
  // and also evenly within each authority
  size_t target_worker = (id / authorities.size()) % num_workers_per_authority[target_authority];

  std::string target_worker_name = std::to_string(target_worker);
  std::string target_addr = committee_json["authorities"][authorities[target_authority]]["workers"][target_worker_name]["transactions"];

  TCPTransport temp_tcp_transport;
  client = std::make_unique<rust::Box<AutobahnClient>>(new_client(GetSocketAddr(target_addr, temp_tcp_transport)));
}

void AutobahnAgent::CreateServerInterface(TransportReceiver *receiver) {
  Debug("Starting autobahn server interface");
  std::filesystem::path committee_json_path = config_path;
  committee_json_path /= committee_hostname_filename;
  std::ifstream f1(committee_json_path);
  json committee_json = json::parse(f1);

  // modify hostname:port into socket addr because autobahn expects this
  TCPTransport temp_tcp_transport;
  for (auto& [key, value] : committee_json["authorities"].items()) {
    value["consensus"]["consensus_to_consensus"] = GetSocketAddr(value["consensus"]["consensus_to_consensus"], temp_tcp_transport);
    value["primary"]["primary_to_primary"] = GetSocketAddr(value["primary"]["primary_to_primary"], temp_tcp_transport);
    value["primary"]["worker_to_primary"] = GetSocketAddr(value["primary"]["worker_to_primary"], temp_tcp_transport);

    for (auto& [worker_name, worker_info] : value["workers"].items()) {
      worker_info["primary_to_worker"] = GetSocketAddr(worker_info["primary_to_worker"], temp_tcp_transport);
      worker_info["transactions"] = GetSocketAddr(worker_info["transactions"], temp_tcp_transport);
      worker_info["worker_to_worker"] = GetSocketAddr(worker_info["worker_to_worker"], temp_tcp_transport);
    }
  }

  std::filesystem::path committee_json_out_path = config_path;
  committee_json_out_path /= ".committee.json";
  std::ofstream committee_json_out(committee_json_out_path);
  committee_json_out << committee_json.dump(2) << std::endl;
  committee_json_out.close();

  std::string node_filename = ".node-" + std::to_string(id) + ".json";
  std::filesystem::path node_json_path = config_path;
  node_json_path /= node_filename;
  std::ifstream f2(node_json_path);
  json node_json = json::parse(f2);

  std::filesystem::path parameter_json_path = config_path;
  parameter_json_path /= ".parameters.json";

  int64_t handle = reinterpret_cast<int64_t>(receiver);

  std::string db_name = ".db-" + std::to_string(id);
  std::filesystem::path primary_db_path = config_path;
  primary_db_path /= db_name;

  // start primary
  primary = std::make_unique<rust::Box<AutobahnServer>>(new_server());
  (*primary)->start_server(
    handle,
    node_json_path.string(),
    committee_json_out_path.string(),
    parameter_json_path.string(),
    primary_db_path.string(),
    true,
    0 // ignored for primary
  );

  Debug("Finished starting primary");

  // start workers
  for (size_t i = 0; i < committee_json["authorities"][node_json["name"]]["workers"].size(); ++i) {
    std::string worker_db_name = ".db-" + std::to_string(id) + "-" + std::to_string(i);
    std::filesystem::path worker_db_path = config_path;
    worker_db_path /= worker_db_name;
    workers.emplace_back(std::make_unique<rust::Box<AutobahnServer>>(new_server()));
    (*workers[i])->start_server(
      handle,
      node_json_path.string(),
      committee_json_out_path.string(),
      parameter_json_path.string(),
      worker_db_path.string(),
      false,
      i
    );
  }
}

void AutobahnAgent::SendMessageToGroup(int group_idx, void *buffer, size_t size) {
  UW_ASSERT(is_client);
  (*client)->send(rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(buffer), size));
}

std::string AutobahnAgent::GetSocketAddr(const std::string &host_port, TCPTransport &tcp_transport) {
  size_t split = host_port.find(":");
  if (split == std::string::npos) {
    throw std::invalid_argument("Invalid host:port format");
  }
  std::string host = host_port.substr(0, split);
  std::string port = host_port.substr(split + 1);

  transport::ReplicaAddress addr = transport::ReplicaAddress(host, port);
  TCPTransportAddress socket_addr = tcp_transport.LookupAddress(addr);
  std::string out(inet_ntoa(socket_addr.addr.sin_addr));
  out.push_back(':');
  out.append(std::to_string(ntohs(socket_addr.addr.sin_port)));
  return out;
}

} // namespace autobahn
