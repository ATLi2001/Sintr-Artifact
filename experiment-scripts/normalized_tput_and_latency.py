import pandas as pd
import matplotlib.pyplot as plt
import sys
import os
import datetime

def main():
    # Base experiment results path
    base_path = "experiment-results/overheads"

    # Create a timestamped subfolder (e.g. 2025-09-26_14-30-55)
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    exp_path = os.path.join(base_path, timestamp)

    # Create the directory
    os.makedirs(exp_path, exist_ok=True)

    if len(sys.argv) != 2 and len(sys.argv) != 3:
        print(f"Usage: python {os.path.basename(__file__)} <csv_file> [specific_client_num]")
        sys.exit(1)
    specific_client_num = -1
    if len(sys.argv) == 3:
        specific_client_num = int(sys.argv[2])

    csv_path = sys.argv[1]

    if not os.path.isfile(csv_path):
        print(f"Error: File '{csv_path}' not found.")
        sys.exit(1)

    # Load CSV
    df = pd.read_csv(csv_path)

    if df.empty:
        print("Error: CSV file is empty.")
        sys.exit(1)

    required_columns = {"experiment_name", "num_clients", "tput", "latency"}
    if not required_columns.issubset(df.columns):
        print(f"Error: CSV must contain the following columns: {required_columns}")
        sys.exit(1)

    # Group by experiment and client count
    grouped = df.groupby(["experiment_name", "num_clients"], sort=False).agg({
        "tput": "mean",
        "latency": "mean"
    }).reset_index()

    grouped = grouped.rename(columns={"tput": "avg_tput", "latency": "avg_latency"})

    normalized_records = []

    # Group by num_clients
    for client_count, group in grouped.groupby("num_clients", sort=False):
        if specific_client_num != -1 and specific_client_num != client_count:
            continue
        
        # Keep original order
        sorted_group = group.reset_index(drop=True)
        
        # Get the first experiment as baseline for normalization
        baseline_experiment = sorted_group.iloc[0]
        baseline_tput = baseline_experiment["avg_tput"]
        baseline_latency = baseline_experiment["avg_latency"]
        baseline_name = baseline_experiment["experiment_name"]

        print(f"Baseline for {client_count} clients: '{baseline_name}' (tput={baseline_tput:.2f}, latency={baseline_latency:.2f})")

        for _, row in sorted_group.iterrows():
            # Normalize relative to baseline (first experiment)
            normalized_tput = row["avg_tput"] / baseline_tput
            normalized_latency = row["avg_latency"] / baseline_latency

            normalized_records.append({
                "experiment_name": row["experiment_name"],
                "num_clients": row["num_clients"],
                "avg_tput": row["avg_tput"],
                "avg_latency": row["avg_latency"],
                "normalized_tput": normalized_tput,
                "normalized_latency": normalized_latency,
                "baseline_experiment": baseline_name
            })

    # Convert to DataFrame
    normalized_df = pd.DataFrame(normalized_records)

    # Save CSV
    output_csv = os.path.join(exp_path, "normalized_metrics_per_client_group.csv")
    normalized_df.to_csv(output_csv, index=False)
    print(f"\nNormalized metrics saved to: {output_csv}")

    # ------------------------------
    # Plotting (Normalized tput & latency)
    # ------------------------------
    # Add labels
    normalized_df["label"] = normalized_df.apply(
        lambda row: f"{row['experiment_name']} ({int(row['num_clients'])} clients)", axis=1
    )

    # Sort by client and experiment for cleaner plots
    # If specific client number is provided, preserve original order from CSV
    if specific_client_num == -1:
        normalized_df = normalized_df.sort_values(by=["num_clients", "experiment_name"])
    # else: keep the order as it was added (which matches the CSV order)

    # Plot normalized_tput
    plt.figure(figsize=(12, 6))
    plt.bar(normalized_df["label"], normalized_df["normalized_tput"], color='skyblue')
    plt.axhline(y=1.0, color='red', linestyle='--', linewidth=1, label='Baseline')
    plt.title("Normalized Throughput per Experiment and Client Count")
    plt.ylabel("Normalized Throughput (relative to first experiment)")
    plt.ylim(bottom=1.0)
    plt.xticks(rotation=45, ha='right')
    plt.legend()
    plt.tight_layout()
    tput_plot_path = os.path.join(exp_path, "normalized_tput_barplot.png")
    plt.savefig(tput_plot_path)
    print(f"Normalized throughput bar plot saved as '{tput_plot_path}'")

    # Plot normalized_latency
    plt.figure(figsize=(12, 6))
    plt.bar(normalized_df["label"], normalized_df["normalized_latency"], color='salmon')
    plt.axhline(y=1.0, color='red', linestyle='--', linewidth=1, label='Baseline')
    plt.title("Normalized Latency per Experiment and Client Count")
    plt.ylabel("Normalized Latency (relative to first experiment)")
    plt.ylim(top=1.0)
    plt.xticks(rotation=45, ha='right')
    plt.legend()
    plt.tight_layout()
    lat_plot_path = os.path.join(exp_path, "normalized_latency_barplot.png")
    plt.savefig(lat_plot_path)
    print(f"Normalized latency bar plot saved as '{lat_plot_path}'")

if __name__ == "__main__":
    main()
