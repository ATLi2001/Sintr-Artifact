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
PREPROCESS_DIR = "experiment-results/preprocessed"
OUTPUT_DIR = "experiment-results/analyzed"
ANALYSIS_TYPES = ["latency_throughput", "sig_nosig_tput_bar", "sig_nosig_lat_bar"]


# collect original stats.json files and places them into preprocess_dir under unique names
def preprocess_original_stats(original_stats_dir, preprocess_dir):
    for subdir in os.listdir(original_stats_dir):
        for file in os.listdir(os.path.join(original_stats_dir, subdir)):
            # read in config file
            if file != "stats.json" and file.endswith(".json"):
                with open(os.path.join(original_stats_dir, subdir, file), "r") as f:
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
                    # copy the stats.json file to the preprocess_dir with the unique name
                    shutil.copyfile(
                        os.path.join(original_stats_dir, subdir, "stats.json"),
                        os.path.join(preprocess_dir, unique_name)
                    )

# reads all stats.json files in the given directory and returns a dictionary of dictionaries
# each dictionary corresponds to a stats.json file
# expected name format is <experiment_name>_<num_clients>_<timestamp>.json
def read_stats_files(preprocess_dir):
    out = {}

    for file in os.listdir(preprocess_dir):
        if file.endswith(".json"):
            file_path = os.path.join(preprocess_dir, file)
            if os.path.isfile(file_path):
                with open(file_path, "r") as f:
                    stats = json.load(f)
                    # trim off .json extension from the filename
                    out[file[:-5]] = stats
    
    return out

# converts a dictionary of stats dictionaries to a CSV file.
def stats_to_csv(stats_dicts, output_dir, now_string, save_to_file=True):
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

    if save_to_file:
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

    fig, ax = plt.subplots(layout="constrained")

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

def create_sig_no_sig_bar_plot(df, output_dir, analysis_type, now_string):
    if analysis_type == ANALYSIS_TYPES[1]:
        df_colname = "tput"
        y_label = "Throughput (txn/s)"
    elif analysis_type == ANALYSIS_TYPES[2]:
        df_colname = "latency"
        y_label = "Latency (ms)"
    else:
        raise ValueError(f"Unsupported analysis type: {analysis_type}")

    # dictionary to hold data for sig and no-sig versions
    sig_no_sig_data = {"sig": [], "no-sig": []}
    experiment_labels = []
    for experiment_name, group in df.groupby(["experiment_name"]):
        client_groups = group.groupby("num_clients")
        data = client_groups[df_colname].mean()

        # sig and no-sig versions have the same base experiment name
        if experiment_name[0].endswith("-nosig"):
            base_experiment_name = experiment_name[0][:-6]
            sig_no_sig_data["no-sig"].append(data.values[0])
        else:
            base_experiment_name = experiment_name[0]
            sig_no_sig_data["sig"].append(data.values[0])

        if base_experiment_name not in experiment_labels:
            experiment_labels.append(base_experiment_name)

    sig_no_sig_data["no-sig"].insert(0, 0)
    print(sig_no_sig_data)

    create_grouped_bar_plot(
        sig_no_sig_data,
        experiment_labels,
        y_label,
        output_dir,
        analysis_type,
        now_string
    )


if __name__ == "__main__":
    # this script is used to analyze experiment runs
    # experiment runs produce stats.json files, which should be placed in the original_stats_dir
    # this step is automated through the collect_results.sh script
    # first, the original_stats_dir files are preprocessed by collecting them into the preprocess_dir
    # they are given a unique name indicating the experiment name, number of clients, and timestamp
    # then, preprocessed stats.json files are read in and the relevant data is extracted into a csv
    # depending on the analysis type, the corresponding plot is generated as well

    parser = argparse.ArgumentParser(description="Analyze stats.json files.")
    parser.add_argument(
        "-p", "--preprocess_dir",
        default=PREPROCESS_DIR,
        type=str,
        required=False,
        help=f"Where to look for stats.json files (default: {PREPROCESS_DIR})"
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
    parser.add_argument(
        "-c", "--csv",
        type=str,
        required=False,
        help="Path to csv file that contains the data to analyze. If provided, generates plots from this file instead of going through preprocessed_dir."
    )
    parser.add_argument(
        "--skip_preprocess",
        action="store_true",
        help="If set, only generate csv and plots from current preprocessed files; skips preprocessing from original_stats_dir."
    )
    parser.add_argument(
        "--save_csv",
        action="store_true",
        help="If set, saves the generated CSV file to disk."
    )
    parser.add_argument(
        "--no_save_csv",
        dest="save_csv",
        action="store_false",
        help="If set, does not save the generated CSV file to disk."
    )
    parser.set_defaults(save_csv=True)
    args = parser.parse_args()

    now_string = time.strftime("%Y-%m-%d-%H-%M-%S", time.localtime())
    if not os.path.exists(args.output_dir):
        os.makedirs(args.output_dir)

    if args.csv:
        df = pd.read_csv(args.csv)
    else:
        if not args.skip_preprocess:
            preprocess_original_stats(args.original_stats_dir, args.preprocess_dir)

        stats_dicts = read_stats_files(args.preprocess_dir)
        if len(stats_dicts) == 0:
            print(f"No stats.json files found in {args.preprocess_dir}.")
            exit(1)
        df = stats_to_csv(stats_dicts, args.output_dir, now_string, save_to_file=args.save_csv)

    if args.analysis_type == ANALYSIS_TYPES[0]:
        create_lat_tput_plots(df, args.output_dir, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[1] or args.analysis_type == ANALYSIS_TYPES[2]:
        create_sig_no_sig_bar_plot(df, args.output_dir, args.analysis_type, now_string)
