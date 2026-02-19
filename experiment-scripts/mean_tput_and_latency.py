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

    if len(sys.argv) < 2 or len(sys.argv) > 4:
        print(f"Usage: python {os.path.basename(__file__)} <csv_file> [specific_client_num] [--normalize]")
        sys.exit(1)
    specific_client_num = -1
    normalize = False
    
    # Parse arguments
    for arg in sys.argv[2:]:
        if arg == "--normalize":
            normalize = True
        else:
            specific_client_num = int(arg)

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

    overhead_records = []

    # Group by num_clients
    for client_count, group in grouped.groupby("num_clients", sort=False):
        # Sort to get the highest tput experiment as base
        if specific_client_num != -1 and specific_client_num != client_count:
            continue
        
        # Keep original order
        sorted_group = group.reset_index(drop=True)
        
        # Determine baseline for this client group
        if normalize:
            # For normalization, always use first experiment as baseline
            base_experiment = sorted_group.iloc[0]
        elif specific_client_num != -1:
            # If specific client number provided, use first experiment as baseline
            base_experiment = sorted_group.iloc[0]
        else:
            # Otherwise use highest throughput experiment as base
            base_idx = group["avg_tput"].idxmax()
            base_experiment = group.loc[base_idx]
            
        base_tput = base_experiment["avg_tput"]
        base_latency = base_experiment["avg_latency"]
        base_name = base_experiment["experiment_name"]

        if normalize:
            print(f"Baseline for {client_count} clients: '{base_name}' (tput={base_tput:.2f}, latency={base_latency:.2f})")
        else:
            print(f"Base for {client_count} clients: '{base_name}'")

        for _, row in sorted_group.iterrows():
            if normalize:
                # For normalization, compute normalized values
                record = {
                    "experiment_name": row["experiment_name"],
                    "num_clients": row["num_clients"],
                    "avg_tput": row["avg_tput"],
                    "avg_latency": row["avg_latency"],
                    "normalized_tput": row["avg_tput"] / base_tput,
                    "normalized_latency": row["avg_latency"] / base_latency,
                    "baseline_experiment": base_name
                }
            else:
                # For overhead calculation
                tput_overhead = (base_tput - row["avg_tput"]) / base_tput * 100
                latency_overhead = (row["avg_latency"] - base_latency) / base_latency * 100
                record = {
                    "experiment_name": row["experiment_name"],
                    "num_clients": row["num_clients"],
                    "avg_tput": row["avg_tput"],
                    "avg_latency": row["avg_latency"],
                    "base_experiment": base_name,
                    "tput_overhead_%": tput_overhead,
                    "latency_overhead_%": latency_overhead
                }
            overhead_records.append(record)

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
    # If specific client number is provided, preserve original order from CSV
    if specific_client_num == -1:
        overhead_df = overhead_df.sort_values(by=["num_clients", "experiment_name"])
    # else: keep the order as it was added (which matches the CSV order)

    # Plot based on normalize flag
    if normalize:
        # Plot normalized_tput
        plt.figure(figsize=(12, 6))
        plt.bar(overhead_df["label"], overhead_df["normalized_tput"], color='skyblue')
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
        plt.bar(overhead_df["label"], overhead_df["normalized_latency"], color='salmon')
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
    else:
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
