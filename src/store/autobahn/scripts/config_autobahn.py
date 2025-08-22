"""
 Copyright 2025 Austin Li <atl63@cornell.edu>

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use, copy,
 modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
"""

import json
import os
import argparse
import shutil
import subprocess

# Autobahn parameters copied over
bench_params = {
    'faults': 0,
    'nodes': [4],
    'workers': 1,
    'co-locate': True,
    'rate': [240_000],
    'tx_size': 512,
    'duration': 60,
    'runs': 1,

    # Unused
    'simulate_partition': True,
    'partition_start': 5,
    'partition_duration': 5,
    'partition_nodes': 1,
}
node_params = {
    'timeout_delay': 5_000,  # ms
    'header_size': 32,  # bytes
    'max_header_delay': 5_000,  # ms
    'gc_depth': 50,  # rounds
    'sync_retry_delay': 5_000,  # ms
    'sync_retry_nodes': 3,  # number of nodes
    'batch_size': 500_000,  # bytes
    'max_batch_delay': 20,  # ms
    'use_optimistic_tips': True,
    'use_parallel_proposals': True,
    'k': 4,
    'use_fast_path': True,
    'fast_path_timeout': 5_000,
    'use_ride_share': False,
    'car_timeout': 5_000,

    'simulate_asynchrony': False,
    'asynchrony_start': 15_000, #ms
    'asynchrony_duration': 3_000, #ms
}

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create Autobahn config files.")

    curr_dir = os.path.dirname(os.path.abspath(__file__))

    parser.add_argument(
        "-i", "--input_bin_dir",
        type=str,
        default=f"{curr_dir}/../library/target/release/",
        help="Input directory for the Autobahn binaries."
    )
    parser.add_argument(
        "-o", "--output_dir",
        type=str,
        default=f"{curr_dir}/../config/",
        help="Output directory for the generated config files."
    )
    parser.add_argument(
        "-s", "--server_hosts",
        type=str,
        default=f"{curr_dir}/../config/server-hosts.txt",
        help="Server hostnames file for the Autobahn config files."
    )
    parser.add_argument(
        "-e", "--exp_name",
        type=str,
        default="sintr",
        help="Name of the cloudlab experiment."
    )
    parser.add_argument(
        "-j", "--project_name",
        type=str,
        default="pequin-pg0",
        help="Name of the cloudlab project."
    )
    parser.add_argument(
        "-c", "--cluster",
        type=str,
        default="utah",
        help="Name of the cloudlab cluster."
    )
    parser.add_argument(
        "-l", "--local",
        action="store_true",
        help="Run the Autobahn nodes locally."
    )
    parser.add_argument(
        "-p", "--port",
        type=int,
        default=3000,
        help="Base port number for the Autobahn config files."
    )

    args = parser.parse_args()

    if not os.path.exists(args.output_dir):
        os.makedirs(args.output_dir)

    with open(f"{args.output_dir}/.parameters.json", "w") as f:
        json.dump(node_params, f, indent=2)

    nodes = bench_params['nodes'][0]
    workers = bench_params['workers']

    if args.local:
        hosts = ["127.0.0.1"] * nodes
    else:
        # read in server host names
        cloudlab_suffix = f"{args.exp_name}.{args.project_name}.{args.cluster}.cloudlab.us"
        with open(args.server_hosts, "r") as f:
            hosts = [f"{line.strip()}.{cloudlab_suffix}" for line in f.readlines()]

    if len(hosts) != nodes:
        raise ValueError(f"Expected {nodes} server hosts, but found {len(hosts)}.")

    # generate keys
    names = []
    for i in range(nodes):
        subprocess.run(
            [f"{args.input_bin_dir}/node", "generate_keys", "--filename", f"{args.output_dir}/.node-{i}.json"],
            check=True
        )
        with open(f"{args.output_dir}/.node-{i}.json", "r") as f:
            key_json = json.load(f)
            names.append(key_json["name"])

    # generate committee configuration in the requested format
    committee_json = {"authorities": {}}
    port_counter = args.port
    for i in range(nodes):
        authority = {}
        # Consensus section
        authority["consensus"] = {
            "consensus_to_consensus": f"{hosts[i]}:{port_counter}"
        }
        port_counter += 1
        # Primary section
        authority["primary"] = {
            "primary_to_primary": f"{hosts[i]}:{port_counter}",
            "worker_to_primary": f"{hosts[i]}:{port_counter+1}"
        }
        port_counter += 2
        # Stake
        authority["stake"] = 1
        # Workers section (only worker 0)
        workers_dict = {}
        for j in range(workers):
            workers_dict[str(j)] = {
                "primary_to_worker": f"{hosts[i]}:{port_counter}",
                "transactions": f"{hosts[i]}:{port_counter+1}",
                "worker_to_worker": f"{hosts[i]}:{port_counter+2}"
            }
        port_counter += 3
        authority["workers"] = workers_dict
        committee_json["authorities"][names[i]] = authority

    with open(f"{args.output_dir}/.committee.json", "w") as f:
        json.dump(committee_json, f, indent=2)