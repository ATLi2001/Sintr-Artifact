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


BASE_DIR = "experiment-results"
ORIGINAL_STATS_DIR = f"{BASE_DIR}/original"
OUTPUT_CSV_DIR = f"{BASE_DIR}/analyzed/csv"
OUTPUT_PLOT_DIR = f"{BASE_DIR}/analyzed/plots"
ANALYSIS_TYPES = [
    "latency_throughput",
    "sig_nosig_tput_bar",
    "sig_nosig_lat_bar",
    "overheads_lat_cum_bar",
    "overheads_lat_grouped_bar",
    "throughput_time",
    "client_failures",
]

# the original stats directory should have subdirectories, each corresponding to a single experiment run
# each subdirectory should have a stats.json file and a config json file, and potentially a logs directory
# read these files and generate csvs with the data
def parse_original_stats_dir(original_stats_dir, output_dir, now_string, save_csv=True, save_logs_csv=True):
    overall_stats_df = pd.DataFrame(columns=["experiment_name", "num_clients", "timestamp", "tput", "latency"])

    # for logs, more efficient to use list to collect data, then create dataframe
    data_rows = []
    byz_data_rows = []

    total_recorded_time = 0
    for subdir in os.listdir(original_stats_dir):
        subdir_path = os.path.join(original_stats_dir, subdir)

        analysis_name = None
        num_clients = 0
        num_byz_clients = 0
        for file in os.listdir(subdir_path):
            # read in config file
            if file != "stats.json" and file.endswith(".json"):
                analysis_name, num_clients, num_byz_clients, total_recorded_time = parse_config_file(os.path.join(subdir_path, file))

        tput, latency = parse_stats_json(os.path.join(subdir_path, "stats.json"))
        if tput is None or latency is None:
            continue

        overall_stats_df.loc[len(overall_stats_df)] = [analysis_name, num_clients, subdir, tput, latency]
        
        # Process log files
        logs_dir = os.path.join(subdir_path, "logs")
        if not os.path.exists(logs_dir):
            continue
        log_data_rows, byz_log_data_rows = parse_logs_dir(logs_dir, analysis_name, num_clients, num_byz_clients, subdir)

        data_rows.extend(log_data_rows)
        byz_data_rows.extend(byz_log_data_rows)

    # sort by experiment name and number of clients
    overall_stats_df.sort_values(by=["experiment_name", "num_clients", "timestamp"], inplace=True)
    if save_csv:
        overall_stats_df.to_csv(os.path.join(output_dir, f"{ANALYSIS_TYPES[0]}-{now_string}.csv"), index=False)

    # Create DataFrame once from all collected data
    logs_df = pd.DataFrame(
        data_rows,
        columns=[
            "experiment_name", "operation", "latency_ns", "commit_timestamp_ns",
            "client_id", "num_clients", "num_byz_clients", "exp_timestamp"
        ]
    )
    # Convert timestamp column to numeric for proper sorting
    logs_df["commit_timestamp_ns"] = pd.to_numeric(logs_df["commit_timestamp_ns"], errors="coerce")
    logs_df["latency_ns"] = pd.to_numeric(logs_df["latency_ns"], errors="coerce")

    if len(data_rows) > 0 and save_logs_csv:
        logs_df.to_csv(os.path.join(output_dir, f"logs-{now_string}.csv"), index=False)

    byz_logs_df = pd.DataFrame(
        byz_data_rows,
        columns=[
            "experiment_name", "operation", "latency_ns", "commit_timestamp_ns",
            "client_id", "num_clients", "num_byz_clients", "exp_timestamp"
        ]
    )
    byz_logs_df["commit_timestamp_ns"] = pd.to_numeric(byz_logs_df["commit_timestamp_ns"], errors="coerce")
    byz_logs_df["latency_ns"] = pd.to_numeric(byz_logs_df["latency_ns"], errors="coerce")
    if len(byz_data_rows) > 0 and save_logs_csv:
        byz_logs_df.to_csv(os.path.join(output_dir, f"logs-{now_string}-byz.csv"), index=False)

    return overall_stats_df, logs_df, byz_logs_df, total_recorded_time

# extract information from config json
def parse_config_file(config_path):
    total_recorded_time = 0
    analysis_name = None
    num_clients = 0
    num_byz_clients = 0
    with open(config_path, "r") as config_file:
        config = json.load(config_file)

        if "analysis_name" in config:
            analysis_name = config["analysis_name"]
        else:
            protocol = config["client_protocol_mode"]
            benchmark = config["benchmark_name"]
            analysis_name = f"{protocol}-{benchmark}"
        num_clients = config["client_total"]

        if "sintr_protocol_settings" in config and "sintr_byz_client_total" in config["sintr_protocol_settings"]:
            num_byz_clients = config["sintr_protocol_settings"]["sintr_byz_client_total"]

        total_recorded_time = float(config["client_experiment_length"] - config["client_ramp_up"] - config["client_ramp_down"])

    return analysis_name, num_clients, num_byz_clients, total_recorded_time

# return mean throughput and latency from stats.json file
def parse_stats_json(stats_json_path):
    with open(stats_json_path, "r") as stats_file:
        stats_json = json.load(stats_file)

        if "run_stats" not in stats_json or "combined" not in stats_json["run_stats"]:
            print(f"Skipping {stats_json_path} as it does not contain run_stats or combined data.")
            return None, None

        return stats_json["run_stats"]["combined"]["tput"]["p50"], stats_json["run_stats"]["combined"]["mean"]["p50"]

# read all the logs in a single logs directory
# log files are formatted as operation,latency,timestamp,client_id
# subdir to distinguish between different experiment runs
def parse_logs_dir(logs_dir_path, analysis_name, client_total, num_byz_clients, subdir):
    data_rows = []
    byz_data_rows = []

    # compute which clients should have been byzantine
    byz_client_ids = set()
    if num_byz_clients > 0:
        for i in range(num_byz_clients):
            # evenly space out byzantine clients
            byz_client_ids.add((i * client_total) // num_byz_clients)

    for log_file in sorted(os.listdir(logs_dir_path)):
        log_path = os.path.join(logs_dir_path, log_file)
        # if we are tracking aborts over time, we need to ignore lines before #start
        # started = False
        with open(log_path, "r") as f:
            for line in f:
                line = line.strip()
                # if "#start" in line:
                #     started = True
                #     continue
                # if not started:
                #     continue
                if not line or "#end" in line:
                    break
                operation, latency, timestamp, client_id = line.split(",")
                # log file client ids are a factor of 16 higher
                # this is because each client process can run up to 16 threads which share the same process client id
                # but will have different logged thread client ids
                # for client failure experiments, we only run one thread per process 
                # so byzantine clients are determined by the process client id
                if (int(client_id) / 16) in byz_client_ids:
                    byz_data_rows.append([analysis_name, operation, latency, timestamp, client_id, client_total, num_byz_clients, subdir])
                else:
                    data_rows.append([analysis_name, operation, latency, timestamp, client_id, client_total, num_byz_clients, subdir])

    return data_rows, byz_data_rows

def client_failures_csv(logs_df, total_recorded_time, output_dir, now_string, tput_per_correct=True):
    tput_col_name = "tput_per_correct_client" if tput_per_correct else "tput_per_byz_client"
    out_df = pd.DataFrame(columns=["experiment_name", "num_clients", "num_byz_clients", tput_col_name, "exp_timestamp"])

    for experiment_name, group in logs_df.groupby("experiment_name"):
        total_clients = group["num_clients"].values[0]
        for curr_num_byz_clients, sub_group in group.groupby("num_byz_clients"):
            num_correct_clients = total_clients - curr_num_byz_clients
            client_div = num_correct_clients if tput_per_correct else curr_num_byz_clients
            if client_div == 0:
                continue

            for exp_timestamp, exp_group in sub_group.groupby("exp_timestamp"):
                tput = len(exp_group) / total_recorded_time / client_div

                out_df.loc[len(out_df)] = [
                    experiment_name,
                    total_clients,
                    curr_num_byz_clients,
                    tput,
                    exp_timestamp
                ]

    # sort by experiment name and number of byzantine clients
    out_df.sort_values(by=["experiment_name", "num_byz_clients", "exp_timestamp"], inplace=True)
    out_df.to_csv(os.path.join(output_dir, f"{ANALYSIS_TYPES[6]}-{now_string}.csv"), index=False)
    return out_df


def create_lat_tput_plots(df, output_dir, now_string):
    fig, ax = plt.subplots(layout="constrained")
    ax.set_xlabel("Throughput (txn/s)")
    ax.set_ylabel("Latency (ms)")
    ax.grid(True)

    for experiment_name, group in df.groupby(["experiment_name"]):
        client_groups = group.groupby("num_clients")
        tput = client_groups["tput"].mean()
        latency = client_groups["latency"].mean()
        ax.plot(tput, latency, "-o", label=experiment_name[0])
    fig.legend(loc="outside lower center", ncol=2)
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

def create_tput_time_plot(df, output_dir, now_string):
    fig, ax = plt.subplots(layout="constrained")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Throughput (txn/s)")
    ax.grid(True)

    df["commit_timestamp_ns"] = df["commit_timestamp_ns"].astype(float)
    df["commit_timestamp_ns"] = df["commit_timestamp_ns"] / 1e9

    policy_change_time_s = -1
    tput_interval_s = 2
    plot_abort = False
    plot_latency = False
    for experiment_name, group in df.groupby(["experiment_name"]):
        # group by client_id and normalize each client's time to start at 0
        # then combine all clients' data
        # this ensures that we are not affected by clock skew between clients
        combined_group = pd.DataFrame()
        combined_abort = pd.DataFrame()
        for client_id, client_group in group.groupby("client_id"):
            client_group = client_group.sort_values(by=["commit_timestamp_ns"])
            t0 = client_group["commit_timestamp_ns"].iloc[0]
            client_group["commit_timestamp_ns"] = client_group["commit_timestamp_ns"] - t0
            if int(client_id) == 0:
                # find policy change time
                policy_change = client_group[client_group["operation"] == "policy_change"]
                if len(policy_change) > 0:
                    policy_change_time_s = policy_change["commit_timestamp_ns"].iloc[0]
                    print(f"{experiment_name[0]}: policy change at {policy_change_time_s:.2f}s")

            combined_group = pd.concat([combined_group, client_group.loc[client_group["operation"] != "abort"]])
            combined_abort = pd.concat([combined_abort, client_group.loc[client_group["operation"] == "abort"]])

        # calculate throughput at intervals
        time_bins = np.arange(0, combined_group["commit_timestamp_ns"].max(), tput_interval_s)
        combined_group["time_bin"] = pd.cut(combined_group["commit_timestamp_ns"], bins=time_bins, right=False)
        # throughput is number of transactions in bin
        tput = combined_group.groupby("time_bin").size() / tput_interval_s
        # average latency in bin in ms
        latency = combined_group.groupby("time_bin")["latency_ns"].mean() / 1e6

        overall_tput = len(combined_group) / combined_group["commit_timestamp_ns"].max()
        print(f"{experiment_name[0]}: avg tput {overall_tput:.2f} txn/s")

        ax.plot(time_bins[1:], tput, "-", label=experiment_name[0])

        if plot_latency:
            # also plot latency over time on secondary y-axis
            ax_lat = ax.twinx()
            ax_lat.set_ylabel("Latency (ms)", color="green")
            ax_lat.plot(time_bins[1:], latency, "--", label=f"{experiment_name[0]} latency", color="green")
            ax_lat.tick_params(axis='y', labelcolor="green")

        if plot_abort:
            ax2 = ax.twinx()
            ax2.set_ylabel("Aborts", color="red")
            ax2.plot(combined_abort["commit_timestamp_ns"], np.repeat(1, len(combined_abort)), "o", label=f"{experiment_name[0]} aborts", color="red")

    if policy_change_time_s > 0:
        ax.axvline(x=policy_change_time_s, color="black", linestyle="--", label="Policy Change")

    fig.legend(loc="outside lower center", ncol=2)
    plt.savefig(os.path.join(output_dir, f"{ANALYSIS_TYPES[5]}-{now_string}.png"))
    plt.close()

def create_client_failures_plot(client_failures_df, byz_client_df, output_dir, now_string, combined=False):
    fig, ax = plt.subplots(layout="constrained")
    ax.set_xlabel("# Byzantine Clients")
    if combined:
        ax.set_ylabel("Throughput per Client (txn/s)")
    else:
        ax.set_ylabel(f"Throughput per Correct Client (txn/s)")
    ax.grid(True)

    for experiment_name, group in client_failures_df.groupby("experiment_name"):
        client_groups = group.groupby("num_byz_clients")
        num_byz_clients = client_groups["num_byz_clients"].mean()
        tput_per_correct_client = client_groups["tput_per_correct_client"].mean()
        ax.plot(num_byz_clients, tput_per_correct_client, "-o", label=experiment_name)
    
    if combined:
        for experiment_name, group in byz_client_df.groupby("experiment_name"):
            client_groups = group.groupby("num_byz_clients")
            num_byz_clients = client_groups["num_byz_clients"].mean()
            tput_per_byz_client = client_groups["tput_per_byz_client"].mean()
            ax.plot(num_byz_clients, tput_per_byz_client, "-o", label=experiment_name + " (Byz)")

    fig.legend(loc="outside lower center", ncol=2)
    ylims = ax.get_ylim()
    ax.set_ylim(0, ylims[1] + 10)
    plt.savefig(os.path.join(output_dir, f"{ANALYSIS_TYPES[6]}-{now_string}.png"))
    plt.close()


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
    parser.add_argument(
        "-l", "--logs",
        type=str,
        required=False,
        help="Path to directory that contains logs to analyze for throughput over time plot. If provided, generates plot from these logs instead of going through original_stats_dir."
    )
    args = parser.parse_args()

    now_string = time.strftime("%Y-%m-%d-%H-%M-%S", time.localtime())
    if not os.path.exists(args.output_csv_dir):
        os.makedirs(args.output_csv_dir)
    if not os.path.exists(args.output_plot_dir):
        os.makedirs(args.output_plot_dir)

    df = pd.DataFrame()
    logs_df = pd.DataFrame()
    if args.csv:
        df = pd.read_csv(args.csv)
    if args.logs:
        logs_df = pd.read_csv(args.logs)
    if not args.csv and not args.logs:
        df, logs_df, byz_logs_df, total_recorded_time = parse_original_stats_dir(args.original_stats_dir, args.output_csv_dir, now_string)

    if args.analysis_type == ANALYSIS_TYPES[0]:
        create_lat_tput_plots(df, args.output_plot_dir, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[1] or args.analysis_type == ANALYSIS_TYPES[2]:
        create_sig_no_sig_bar_plot(df, args.output_plot_dir, args.analysis_type, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[3]:
        create_overheads_lat_cum_bar_plot(df, args.output_plot_dir, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[4]:
        create_overheads_lat_grouped_bar_plot(df, args.output_plot_dir, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[5]:
        create_tput_time_plot(logs_df, args.output_plot_dir, now_string)
    elif args.analysis_type == ANALYSIS_TYPES[6]:
        client_failures_df = client_failures_csv(logs_df, total_recorded_time, args.output_csv_dir, now_string)
        byz_client_df = client_failures_csv(byz_logs_df, total_recorded_time, args.output_csv_dir, now_string + "-byz", tput_per_correct=False)
        create_client_failures_plot(client_failures_df, byz_client_df, args.output_plot_dir, now_string, combined=True)
