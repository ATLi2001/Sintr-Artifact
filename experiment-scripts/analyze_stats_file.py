"""
 Copyright 2024 Austin Li <atl63@cornell.edu>

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

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import json
import os
import argparse
import time
import shutil


ORIGINAL_STATS_DIR = "experiment-results/original"
RESULTS_DIR = "experiment-results/stats_json"
OUTPUT_DIR = "experiment-results/analyzed"
ANALYSIS_TYPES = ["latency_throughput", "throughput_bar"]


# collect original stats.json files and places them into results_dir under unique names
def collect_original_stats(original_stats_dir, results_dir):
    for subdir in os.listdir(original_stats_dir):
        for file in os.listdir(os.path.join(original_stats_dir, subdir)):
            # read in config file
            if file != "stats.json" and file.endswith(".json"):
                with open(os.path.join(original_stats_dir, subdir, file), 'r') as f:
                    config = json.load(f)

                    if "analysis_name" in config:
                        analysis_name = config["analysis_name"]
                    else:
                        protocol = config["client_protocol_mode"]
                        benchmark = config["benchmark_name"]
                        analysis_name = f"{protocol}-{benchmark}"
                    num_clients = config["client_total"]
                    # create a unique name for the stats file
                    unique_name = f"{analysis_name}_{num_clients}_{subdir}.json"
                    # copy the stats.json file to the results_dir with the unique name
                    shutil.copyfile(
                        os.path.join(original_stats_dir, subdir, "stats.json"),
                        os.path.join(results_dir, unique_name)
                    )

# reads all stats.json files in the given directory and returns a dictionary of dictionaries.
# each dictionary corresponds to a stats.json file.
# expected name format is <experiment_name>_<num_clients>.json
def read_stats_files(results_dir):
    out = {}

    for file in os.listdir(results_dir):
        if file.endswith(".json"):
            file_path = os.path.join(results_dir, file)
            if os.path.isfile(file_path):
                with open(file_path, 'r') as f:
                    stats = json.load(f)
                    # trim off .json extension from the filename
                    out[file[:-5]] = stats
    
    return out

# converts a dictionary of stats dictionaries to a latency-throughput CSV file.
def stats_to_lat_tput_csv(stats_dicts, output_dir, now_string):
    out_df = pd.DataFrame(columns=["experiment_name", "num_clients", "timestamp", "tput", "latency"])

    for name, stat_json in stats_dicts.items():
        experiment_name, num_clients, timestamp = name.split("_")
        num_clients = int(num_clients)
        out_df.loc[len(out_df)] = [
            experiment_name,
            num_clients,
            timestamp,
            stat_json["run_stats"]["combined"]["tput"]["p50"],
            stat_json["run_stats"]["combined"]["mean"]["p50"]
        ]

    # sort by experiment name and number of clients
    out_df.sort_values(by=["experiment_name", "num_clients"], inplace=True)

    out_df.to_csv(os.path.join(output_dir, f"{ANALYSIS_TYPES[0]}-{now_string}.csv"), index=False)
    return out_df


def create_lat_tput_plots(df, output_dir, now_string):
    plt.xlabel("Throughput (txn/s)")
    plt.ylabel("Latency (ms)")
    plt.grid(True)

    for experiment_name, group in df.groupby(["experiment_name"]):
        client_groups = group.groupby("num_clients")
        tput = client_groups["tput"].mean()
        latency = client_groups["latency"].mean()
        plt.plot(tput, latency, "-o", label=experiment_name[0])
    plt.legend()
    plt.savefig(os.path.join(output_dir, f"{ANALYSIS_TYPES[0]}-{now_string}.png"))
    plt.close()

# grouped_data is a dictionary where keys are attributes (e.g., "sig", "no-sig") and values are lists of measurements
# x_labels is a list of labels for the x-axis
# grouped_data values should be the same length as x_labels
def create_grouped_bar_plot(grouped_data, x_labels, y_label, output_dir, analysis_type, now_string):
    x = np.arange(len(x_labels))  # the label locations
    width = 0.25  # the width of the bars
    multiplier = 0

    fig, ax = plt.subplots(layout='constrained')

    for attribute, measurement in grouped_data.items():
        print(attribute, measurement)
        offset = width * multiplier
        rects = ax.bar(x + offset, measurement, width, label=attribute)
        ax.bar_label(rects, padding=3)
        multiplier += 1

    # Add some text for labels, title and custom x-axis tick labels, etc.
    ax.set_ylabel(y_label)
    ax.set_xticks(x + width, x_labels)
    ax.legend()

    plt.savefig(os.path.join(output_dir, f"{analysis_type}-{now_string}.png"))
    plt.close()

def create_throughput_bar_plot(df, output_dir, now_string):
    # dictionary from base experiment name to list of sig and no-sig throughput
    sig_no_sig_tput = {"sig": [], "no-sig": []}
    experiment_labels = []
    for experiment_name, group in df.groupby(["experiment_name"]):
        client_groups = group.groupby("num_clients")
        tput = client_groups["tput"].mean()

        # sig and no-sig versions have the same base experiment name
        if experiment_name[0].endswith("-nosig"):
            base_experiment_name = experiment_name[0][:-6]
            sig_no_sig_tput["no-sig"].append(tput.values[0])
        else:
            base_experiment_name = experiment_name[0]
            sig_no_sig_tput["sig"].append(tput.values[0])
        
        if base_experiment_name not in experiment_labels:
            experiment_labels.append(base_experiment_name)
    
    sig_no_sig_tput["no-sig"].insert(0, 0)  
    print(sig_no_sig_tput)

    create_grouped_bar_plot(
        sig_no_sig_tput,
        experiment_labels,
        "Throughput (txn/s)",
        output_dir,
        ANALYSIS_TYPES[1],
        now_string
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Combine stats.json files to a CSV.")
    parser.add_argument(
        "-r", "--results_dir",
        default=RESULTS_DIR,
        type=str,
        required=False,
        help=f"Where to look for stats.json files (default: {RESULTS_DIR})"
    )
    parser.add_argument(
        "-o", "--output_dir",
        default=OUTPUT_DIR,
        type=str,
        required=False,
        help=f"Where to write the output files (default: {OUTPUT_DIR})"
    )
    parser.add_argument(
        "-s", "--original_stats_dir",
        default=ORIGINAL_STATS_DIR,
        type=str,
        required=False,
        help=f"Where to look for original stats.json files (default: {ORIGINAL_STATS_DIR})"
    )
    parser.add_argument(
        "-t", "--analysis_type",
        default=ANALYSIS_TYPES[0],
        choices=ANALYSIS_TYPES,
        type=str,
        required=False,
        help=f"Type of analysis to perform (default: {ANALYSIS_TYPES[0]})"
    )

    args = parser.parse_args()

    now_string = time.strftime('%Y-%m-%d-%H-%M-%S', time.localtime())

    collect_original_stats(args.original_stats_dir, args.results_dir)
    stats_dicts = read_stats_files(args.results_dir)
    if args.analysis_type == ANALYSIS_TYPES[0]:
        lat_tput_df = stats_to_lat_tput_csv(stats_dicts, args.output_dir, now_string)
        print(f"Converted {len(stats_dicts)} stats.json files to {args.output_dir}/{ANALYSIS_TYPES[0]}.csv")    
        create_lat_tput_plots(lat_tput_df, args.output_dir, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[1]:
        lat_tput_df = stats_to_lat_tput_csv(stats_dicts, args.output_dir, now_string)
        print(f"Converted {len(stats_dicts)} stats.json files to {args.output_dir}/{ANALYSIS_TYPES[0]}.csv")
        create_throughput_bar_plot(lat_tput_df, args.output_dir, now_string)
