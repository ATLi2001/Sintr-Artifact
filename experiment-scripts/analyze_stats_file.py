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


ORIGINAL_STATS_DIR = "experiment-results/original"
OUTPUT_CSV_DIR = "experiment-results/analyzed/csv"
OUTPUT_PLOT_DIR = "experiment-results/analyzed/plots"
ANALYSIS_TYPES = [
    "latency_throughput",
    "sig_nosig_tput_bar",
    "sig_nosig_lat_bar",
    "overheads_lat_cum_bar",
    "overheads_lat_grouped_bar",
]


# reads all stats.json files in the given directory and returns a dictionary of dictionaries
# each dictionary corresponds to a stats.json file
def read_original_stats(original_stats_dir):
    out = {}

    for subdir in os.listdir(original_stats_dir):
        for file in os.listdir(os.path.join(original_stats_dir, subdir)):
            # read in config file
            if file != "stats.json" and file.endswith(".json"):
                with open(os.path.join(original_stats_dir, subdir, file), "r") as config_file:
                    config = json.load(config_file)

                    if "analysis_name" in config:
                        analysis_name = config["analysis_name"]
                    else:
                        protocol = config["client_protocol_mode"]
                        benchmark = config["benchmark_name"]
                        analysis_name = f"{protocol}-{benchmark}"
                    num_clients = config["client_total"]
                    # create a unique name for the stats file
                    unique_name = f"{analysis_name}_{num_clients}_{subdir}"

                    with open(os.path.join(original_stats_dir, subdir, "stats.json"), "r") as stats_file:
                        stats = json.load(stats_file)
                        out[unique_name] = stats
    return out

# converts a dictionary of stats dictionaries to a CSV file.
def stats_to_csv(stats_dicts, output_dir, now_string):
    out_df = pd.DataFrame(columns=["experiment_name", "num_clients", "timestamp", "tput", "latency"])

    for name, stat_json in stats_dicts.items():
        if "run_stats" not in stat_json or "combined" not in stat_json["run_stats"]:
            print(f"Skipping {name} as it does not contain run_stats or combined data.")
            continue
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
def create_grouped_bar_plot(grouped_data, x_labels, y_label, output_dir, analysis_type, now_string, grouped_yerr=None):
    # spacing if too many bars per group
    bars_per_group = len(grouped_data)
    x = np.arange(len(x_labels)) * (bars_per_group // 4 + 1)  # the label locations
    width = 0.25  # the width of the bars
    multiplier = 0

    fig, ax = plt.subplots(layout="constrained")

    for attribute, measurement in grouped_data.items():
        offset = width * multiplier
        if grouped_yerr:
            rects = ax.bar(x + offset, measurement, width, label=attribute, yerr=grouped_yerr[attribute], capsize=5)
        else:
            rects = ax.bar(x + offset, measurement, width, label=attribute)
        ax.bar_label(rects, label_type="center", fmt="%.2f")
        multiplier += 1

    # Add some text for labels, title and custom x-axis tick labels, etc.
    ax.set_ylabel(y_label)
    ax.set_xticks(x + width, x_labels)
    fig.legend(loc="outside lower center", ncol=2)

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

# grouped_data is a dictionary where keys are attributes and values are lists of measurements
# x_labels is a list of labels for the x-axis
# grouped_data values should be the same length as x_labels
def create_subtractive_cumulative_bar_plot(top_baseline, bottom_baseline, grouped_data, x_labels, y_label, output_dir, analysis_type, now_string):
    x = np.arange(len(x_labels))
    fig, ax = plt.subplots(layout="constrained")

    # base width of bar
    width = 0.25

    # first, plot the top baseline
    rects = ax.bar(x, top_baseline, label="Top Baseline", width=width, align="edge")
    ax.bar_label(rects, fmt="%.4f")
    
    bottom = top_baseline
    for attribute, measurement in reversed(grouped_data.items()):
        measurement = np.array(measurement) * -1  # invert the measurement for subtractive plot
        rects = ax.bar(x, measurement, label=attribute, bottom=bottom, width=width, align="edge")
        if np.all(np.abs(measurement) > 0.05):
            ax.bar_label(rects, labels=np.round(np.abs(measurement), 4), label_type="center", fmt="%.4f")
        bottom += measurement

    # then, plot the bottom baseline
    bottom_rects = ax.bar(x + width, bottom_baseline, label="Bottom Baseline", width=width, align="edge")
    ax.bar_label(bottom_rects, label_type="center", fmt="%.4f")

    ax.set_ylabel(y_label)
    ax.set_xticks(x + width, x_labels)
    fig.legend(loc="outside lower center", ncol=2)

    plt.savefig(os.path.join(output_dir, f"{analysis_type}-{now_string}.png"))
    plt.close()

def create_cumulative_bar_plot(grouped_data, x_labels, y_label, output_dir, analysis_type, now_string):
    x = np.arange(len(x_labels))
    fig, ax = plt.subplots(layout="constrained")

    bottom = np.zeros(len(x_labels))
    for attribute, measurement in grouped_data.items():
        measurement = np.array(measurement)
        rects = ax.bar(x, measurement, label=attribute, bottom=bottom)
        if np.all(measurement > 0.05):
            ax.bar_label(rects, label_type="center", fmt="%.4f")
        bottom += measurement

    ax.set_ylabel(y_label)
    ax.set_xticks(x, x_labels)
    fig.legend(loc="outside lower center", ncol=2)

    plt.savefig(os.path.join(output_dir, f"{analysis_type}-{now_string}.png"))
    plt.close()

def create_overheads_lat_cum_bar_plot(df, output_dir, now_string):
    # for making cumulative bar plot
    stacked_data = {}
    x_labels = ["policy1R-2R", "policy2R-3R"]
    # label_dict = {
    #     0: "Baseline",
    #     1: "Other",
    #     2: "Check Query Result Evidence",
    #     3: "HMAC",
    #     4: "Client Sign/Check Endorsement",
    #     5: "Server Endorsement Check",
    #     6: "Hide TS, Hash Endorsement",
    # }
    label_dict = {
        0: "- Check Query Result Evidence",
        1: "- HMAC",
        2: "- Client Sign/Check Endorsement",
        3: "- Server Endorsement Check",
        4: "- Hide TS, Hash Endorsement",
    }

    suffix_search = [["policy1R-2", "policy2R"], ["policy2R-5", "policy3R"]]
    # for suffixes in suffix_search:
    #     curr_df = df.loc[df["experiment_name"].str.contains("|".join(suffixes))]
    #     mean_latency = curr_df.groupby("experiment_name")["latency"].mean()
    #     mean_latency.sort_values(inplace=True)
    #     diff_latency = mean_latency.diff()

    #     stacked_data.setdefault(label_dict[0], []).append(mean_latency.iloc[0])
    #     i = 1
    #     for latency in diff_latency.values[1:]:
    #         stacked_data.setdefault(label_dict[i], []).append(latency)
    #         i += 1

    top_baseline = [0, 0]
    bottom_baseline = [0, 0]
    for i, suffixes in enumerate(suffix_search):
        curr_df = df.loc[df["experiment_name"].str.contains("|".join(suffixes))]
        mean_latency = curr_df.groupby("experiment_name")["latency"].mean()
        mean_latency.sort_values(inplace=True)
        diff_latency = mean_latency.diff()
        top_baseline[i] = mean_latency.iloc[-1]
        bottom_baseline[i] = mean_latency.iloc[0]

        j = 0
        for latency in diff_latency.values[2:]:
            stacked_data.setdefault(label_dict[j], []).append(latency)
            j += 1

    # create_cumulative_bar_plot(
    #     stacked_data,
    #     x_labels,
    #     "Latency (ms)",
    #     output_dir,
    #     ANALYSIS_TYPES[3],
    #     now_string
    # )

    create_subtractive_cumulative_bar_plot(
        top_baseline,
        bottom_baseline,
        stacked_data,
        x_labels,
        "Latency (ms)",
        output_dir,
        ANALYSIS_TYPES[3],
        now_string
    )

def create_overheads_lat_grouped_bar_plot(df, output_dir, now_string):
    # for making grouped bar plot
    grouped_data = {}
    grouped_err = {}
    # x_labels = ["policy1R-2R", "policy2R-3R"]
    # label_dict = {
    #     0: "Policy+1",
    #     1: "- Hide TS, Hash Endorsement",
    #     2: "- Server Endorsement Check",
    #     3: "- Client Sign/Check Endorsement",
    #     4: "- HMAC",
    #     5: "- Check Query Result Evidence",
    #     6: "Baseline Policy",
    # }

    x_labels = ["pesto-policy1R"]
    label_dict = {
        0: "Policy 1R",
        1: "- Hide TS, Hash Endorsement",
        2: "- Server Endorsement Check",
        3: "- Server Policy CCC",
        4: "- Sort Writeset",
        5: "Pesto",
    }

    # suffix_search = [["policy1R-2", "policy2R"], ["policy2R-5", "policy3R"]]
    suffix_search = [["pesto", "policy1R"]]
    for suffixes in suffix_search:
        curr_df = df.loc[df["experiment_name"].str.contains("|".join(suffixes))]
        mean_latency = curr_df.groupby("experiment_name")["latency"].mean()
        err_latency = curr_df.groupby("experiment_name")["latency"].std()
        mean_latency.sort_values(inplace=True, ascending=False)

        i = 0
        for latency in mean_latency.values:
            grouped_data.setdefault(label_dict[i], []).append(latency)
            grouped_err.setdefault(label_dict[i], []).append(err_latency.loc[mean_latency.index[i]])
            i += 1

    create_grouped_bar_plot(
        grouped_data,
        x_labels,
        "Latency (ms)",
        output_dir,
        ANALYSIS_TYPES[4],
        now_string,
        grouped_yerr=grouped_err
    )


if __name__ == "__main__":
    # this script is used to analyze experiment runs
    # experiment runs produce stats.json files, which should be placed in the original_stats_dir
    # this step is automated through the collect_results.sh script
    # first, the original_stats_dir has subfolders, each of which has configs and stats.json files
    # these files are read in and the relevant data is extracted into a csv
    # depending on the analysis type, the corresponding plot is generated as well

    parser = argparse.ArgumentParser(description="Analyze stats.json files.")
    parser.add_argument(
        "-o", "--output_csv_dir",
        default=OUTPUT_CSV_DIR,
        type=str,
        required=False,
        help=f"Where to write the output csv (default: {OUTPUT_CSV_DIR})"
    )
    parser.add_argument(
        "-p", "--output_plot_dir",
        default=OUTPUT_PLOT_DIR,
        type=str,
        required=False,
        help=f"Where to write the output plots (default: {OUTPUT_PLOT_DIR})"
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
        help="Path to csv file that contains the data to analyze. If provided, generates plots from this file instead of going through original_stats_dir."
    )
    args = parser.parse_args()

    now_string = time.strftime("%Y-%m-%d-%H-%M-%S", time.localtime())
    if not os.path.exists(args.output_csv_dir):
        os.makedirs(args.output_csv_dir)
    if not os.path.exists(args.output_plot_dir):
        os.makedirs(args.output_plot_dir)

    if args.csv:
        df = pd.read_csv(args.csv)
    else:
        stats_dicts = read_original_stats(args.original_stats_dir)
        if len(stats_dicts) == 0:
            print(f"No stats.json files found in {args.original_stats_dir}.")
            exit(1)
        df = stats_to_csv(stats_dicts, args.output_csv_dir, now_string)

    if args.analysis_type == ANALYSIS_TYPES[0]:
        create_lat_tput_plots(df, args.output_plot_dir, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[1] or args.analysis_type == ANALYSIS_TYPES[2]:
        create_sig_no_sig_bar_plot(df, args.output_plot_dir, args.analysis_type, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[3]:
        create_overheads_lat_cum_bar_plot(df, args.output_plot_dir, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[4]:
        create_overheads_lat_grouped_bar_plot(df, args.output_plot_dir, now_string)
