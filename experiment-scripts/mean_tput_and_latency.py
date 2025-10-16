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

    if len(sys.argv) != 2:
        print(f"Usage: python {os.path.basename(__file__)} <csv_file>")
        sys.exit(1)

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
    grouped = df.groupby(["experiment_name", "num_clients"]).agg({
        "tput": "mean",
        "latency": "mean"
    }).reset_index()

    grouped = grouped.rename(columns={"tput": "avg_tput", "latency": "avg_latency"})

    overhead_records = []

    # Group by num_clients
    for client_count, group in grouped.groupby("num_clients"):
        # Sort to get the first experiment as base
        sorted_group = group.sort_values("experiment_name").reset_index(drop=True)
        base_experiment = sorted_group.iloc[0]
        base_tput = base_experiment["avg_tput"]
        base_latency = base_experiment["avg_latency"]
        base_name = base_experiment["experiment_name"]

        print(f"Base for {client_count} clients: '{base_name}'")

        for _, row in sorted_group.iterrows():
            tput_overhead = (base_tput - row["avg_tput"]) / base_tput * 100
            latency_overhead = (row["avg_latency"] - base_latency) / base_latency * 100

            overhead_records.append({
                "experiment_name": row["experiment_name"],
                "num_clients": row["num_clients"],
                "avg_tput": row["avg_tput"],
                "avg_latency": row["avg_latency"],
                "base_experiment": base_name,
                "tput_overhead_%": tput_overhead,
                "latency_overhead_%": latency_overhead
            })

    # Convert to DataFrame
    overhead_df = pd.DataFrame(overhead_records)

    # Save CSV
    output_csv = os.path.join(exp_path, "experiment_overhead_per_client_group.csv")
    overhead_df.to_csv(output_csv, index=False)
    print(f"\nOverhead summary saved to: {output_csv}")

    # ------------------------------
    # Plotting (Average tput & latency)
    # ------------------------------
    # Add labels
    overhead_df["label"] = overhead_df.apply(
        lambda row: f"{row['experiment_name']} ({int(row['num_clients'])} clients)", axis=1
    )

    # Sort by client and experiment for cleaner plots
    overhead_df = overhead_df.sort_values(by=["num_clients", "experiment_name"])

    # Plot avg_tput
    plt.figure(figsize=(12, 6))
    plt.bar(overhead_df["label"], overhead_df["avg_tput"], color='skyblue')
    plt.title("Average Throughput per Experiment and Client Count")
    plt.ylabel("Throughput")
    plt.xticks(rotation=45, ha='right')
    plt.tight_layout()
    tput_plot_path = os.path.join(exp_path, "avg_tput_barplot.png")
    plt.savefig(tput_plot_path)
    print(f"Throughput bar plot saved as '{tput_plot_path}'")

    # Plot avg_latency
    plt.figure(figsize=(12, 6))
    plt.bar(overhead_df["label"], overhead_df["avg_latency"], color='salmon')
    plt.title("Average Latency per Experiment and Client Count")
    plt.ylabel("Latency")
    plt.xticks(rotation=45, ha='right')
    plt.tight_layout()
    lat_plot_path = os.path.join(exp_path, "avg_latency_barplot.png")
    plt.savefig(lat_plot_path)
    print(f"Latency bar plot saved as '{lat_plot_path}'")

if __name__ == "__main__":
    main()
