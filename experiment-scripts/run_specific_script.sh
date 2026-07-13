#!/bin/bash

ROOTDIR="$HOME/Sintr-Artifact"
CONFIG_FILE=$1

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

REMOTE_DIR="/mnt/extra/coredump"

# === Experiment loop ===
for ((i=1; i<=NUM_TRIALS; i++)); do
    echo "Running experiment with configuration: $CONFIG_FILE"
    python3 "$ROOTDIR/experiment-scripts/run_multiple_experiments.py" "$CONFIG_FILE"
    echo "Trial $i complete."
    echo ""
    sleep 1
done
