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
  committee_json_path /= ".committee.json";
  std::ifstream f(committee_json_path);
  json committee_json = json::parse(f);

  std::vector<std::string> authorities;
  for (auto& [key, value] : committee_json["authorities"].items()) {
    authorities.push_back(key);
  }

  size_t target = id % authorities.size();
  std::string target_addr = committee_json["authorities"][authorities[target]]["transactions"];

  client = std::make_unique<rust::Box<Client>>(new_client(target_addr, 32));
}

void AutobahnAgent::CreateServerInterface(TransportReceiver *receiver) {
  std::filesystem::path committee_json_path = config_path;
  committee_json_path /= ".committee.json";
  std::ifstream f1(committee_json_path);
  json committee_json = json::parse(f1);

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
  start_server(
    handle,
    node_json_path.string(),
    committee_json_path.string(),
    parameter_json_path.string(),
    primary_db_path.string(),
    true,
    0 // ignored for primary
  );

  // start workers
  for (size_t i = 0; i < committee_json["authorities"][node_json["name"]]["workers"].size(); ++i) {
    std::string worker_db_name = ".db-" + std::to_string(id) + "-" + std::to_string(i);
    std::filesystem::path worker_db_path = config_path;
    worker_db_path /= worker_db_name;
    start_server(
      handle,
      node_json_path.string(),
      committee_json_path.string(),
      parameter_json_path.string(),
      worker_db_path.string(),
      false,
      i
    );
  }

}

} // namespace autobahn
