#!/bin/bash

ROOTDIR="$HOME/Pequin-Artifact"
CONFIG_FILE=$1
OUTPUT_DIR="$ROOTDIR/output"

# Check if the config file is provided
if [[ -z "$CONFIG_FILE" ]]; then
    echo "Usage: $0 <experiment-config-file> [<num-trials>]"
    exit 1
fi

# Check if the config file exists
if [[ ! -f "$CONFIG_FILE" ]]; then
    echo "Error: Config file '$CONFIG_FILE' does not exist."
    exit 1
fi

NUM_TRIALS=1
if [[ ! -z "$2" ]]; then
    NUM_TRIALS=$2
fi

# === Server list and related config ===
declare -a arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2" "eu-west-1-0" "eu-west-1-1" "eu-west-1-2")
USER="dhl001"
EXP_NAME="sintr-rw"
PROJECT_NAME="pequin"
CLUSTER_NAME="utah"

REMOTE_DIR="/mnt/extra/coredump"

# === Experiment loop ===
for ((i=1; i<=NUM_TRIALS; i++)); do
    echo "========== Trial $i/$NUM_TRIALS =========="

    echo "Preparing /mnt/extra/coredump directory on all servers..."
    for host in "${arr_servers[@]}"; do
        ssh ${USER}@${host}.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us "mkdir -p ${REMOTE_DIR} && rm -rf ${REMOTE_DIR}/*"
    done
    echo "Directory prepared."

    echo "Running experiment with configuration: $CONFIG_FILE"
    python3 "$ROOTDIR/experiment-scripts/run_multiple_experiments.py" "$CONFIG_FILE"
    sleep 2
    echo ""
    echo "=== Trial $i complete. Checking output directory... ==="

    if [[ -d "$OUTPUT_DIR" ]]; then
        if grep -rqiE "error|panic" "$OUTPUT_DIR"; then
            echo "⚠️  Found 'error' or 'panic' in output directory after Trial $i."
            read -p "Do you want to continue to the next trial? [y/N]: " confirm
            if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
                echo "Aborting."
                exit 1
            fi
        else
            # Only clear the output directory every 10 runs
            if (( i % 10 == 0 )); then
                echo "No 'error' or 'panic' found. Clearing output directory (every 10 runs)..."
                rm -rf "$OUTPUT_DIR"/*
            else
                echo "No 'error' or 'panic' found. Keeping output directory (will clear every 10 runs)."
            fi
        fi
    else
        echo "Output directory does not exist. Creating it..."
        mkdir -p "$OUTPUT_DIR"
    fi

    echo ""
done